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

static int
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

static uint32_t
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

    ppriv->backing_bo =
        dumb_get_bo_from_fd(drmmode->fd, fd_handle, pitch, size);
    if (!ppriv->backing_bo)
        return FALSE;

    close(fd_handle);
    return TRUE;
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

#if 0
static PixmapPtr
create_pixmap_for_fbcon(drmmode_ptr drmmode, ScrnInfoPtr pScrn, int crtc_id)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    drmmode_crtc_private_ptr drmmode_crtc;
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pixmap;
    struct radeon_bo *bo;
    drmModeFBPtr fbcon;
    struct drm_gem_flink flink;

    drmmode_crtc = xf86_config->crtc[crtc_id]->driver_private;

    fbcon = drmModeGetFB(drmmode->fd, drmmode_crtc->mode_crtc->buffer_id);
    if (fbcon == NULL)
        return NULL;

    flink.handle = fbcon->handle;
    if (ioctl(drmmode->fd, DRM_IOCTL_GEM_FLINK, &flink) < 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Couldn't flink fbcon handle\n");
        return NULL;
    }

    bo = radeon_bo_open(drmmode->bufmgr, flink.name, 0, 0, 0, 0);
    if (bo == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't allocate bo for fbcon handle\n");
        return NULL;
    }

    pixmap = drmmode_create_bo_pixmap(pScreen, fbcon->width, fbcon->height,
                                      fbcon->depth, fbcon->bpp,
                                      fbcon->pitch, bo);
    if (!pixmap)
        return NULL;

    radeon_bo_unref(bo);
    drmModeFreeFB(fbcon);
    return pixmap;
}

#endif

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
    uint32_t *output_ids;
    int output_count = 0;
    Bool ret = TRUE;
    int i;
    uint32_t fb_id;
    drmModeModeInfo kmode;
    int height;

    height = pScrn->virtualY;

    if (drmmode->fb_id == 0) {
        ret = drmModeAddFB(drmmode->fd,
                           pScrn->virtualX, height,
                           pScrn->depth, pScrn->bitsPerPixel,
                           drmmode_bo_get_pitch(&drmmode->front_bo),
                           drmmode_bo_get_handle(&drmmode->front_bo),
                           &drmmode->fb_id);
        if (ret < 0) {
            ErrorF("failed to add fb %d\n", ret);
            return FALSE;
        }
    }

    saved_mode = crtc->mode;
    saved_x = crtc->x;
    saved_y = crtc->y;
    saved_rotation = crtc->rotation;

    if (mode) {
        crtc->mode = *mode;
        crtc->x = x;
        crtc->y = y;
        crtc->rotation = rotation;
        crtc->transformPresent = FALSE;
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
        if (crtc->randr_crtc->scanout_pixmap) {
            msPixmapPrivPtr ppriv =
                msGetPixmapPriv(drmmode, crtc->randr_crtc->scanout_pixmap);
            fb_id = ppriv->fb_id;
            x = y = 0;
        }
        else if (drmmode_crtc->rotate_fb_id) {
            fb_id = drmmode_crtc->rotate_fb_id;
            x = y = 0;
        }
        ret = drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                             fb_id, x, y, output_ids, output_count, &kmode);
        if (ret)
            xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
                       "failed to set mode: %s", strerror(-ret));
        else
            ret = TRUE;

        if (crtc->scrn->pScreen)
            xf86CrtcSetScreenSubpixelOrder(crtc->scrn->pScreen);

        crtc->funcs->dpms(crtc, DPMSModeOn);

        /* go through all the outputs and force DPMS them back on? */
        for (i = 0; i < xf86_config->num_output; i++) {
            xf86OutputPtr output = xf86_config->output[i];

            if (output->crtc != crtc)
                continue;

            output->funcs->dpms(output, DPMSModeOn);
        }
    }

#if 0
    if (pScrn->pScreen &&
        !xf86ReturnOptValBool(info->Options, OPTION_SW_CURSOR, FALSE))
        xf86_reload_cursors(pScrn->pScreen);
#endif
 done:
    if (!ret) {
        crtc->x = saved_x;
        crtc->y = saved_y;
        crtc->rotation = saved_rotation;
        crtc->mode = saved_mode;
    }
#if defined(XF86_CRTC_VERSION) && XF86_CRTC_VERSION >= 3
    else
        crtc->active = TRUE;
#endif

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

static void
drmmode_set_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t handle = drmmode_crtc->cursor_bo->handle;
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    static Bool use_set_cursor2 = TRUE;
    int ret;

    if (use_set_cursor2) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
        CursorPtr cursor = xf86_config->cursor;

        ret =
            drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                              handle, ms->cursor_width, ms->cursor_height,
                              cursor->bits->xhot, cursor->bits->yhot);
        if (ret == -EINVAL)
            use_set_cursor2 = FALSE;
        else
            return;
    }

    ret = drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, handle,
                           ms->cursor_width, ms->cursor_height);

    if (ret) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
        xf86CursorInfoPtr cursor_info = xf86_config->cursor_info;

        cursor_info->MaxWidth = cursor_info->MaxHeight = 0;
        drmmode_crtc->drmmode->sw_cursor = TRUE;
        /* fallback to swcursor */
    }
}

static void
drmmode_load_cursor_argb(xf86CrtcPtr crtc, CARD32 *image)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    int i;
    uint32_t *ptr;

    /* cursor should be mapped already */
    ptr = (uint32_t *) (drmmode_crtc->cursor_bo->ptr);

    for (i = 0; i < ms->cursor_width * ms->cursor_height; i++)
        ptr[i] = image[i];      // cpu_to_le32(image[i]);

    if (drmmode_crtc->cursor_up)
        drmmode_set_cursor(crtc);
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
drmmode_set_scanout_pixmap(xf86CrtcPtr crtc, PixmapPtr ppix)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    msPixmapPrivPtr ppriv;
    void *ptr;

    if (!ppix) {
        if (crtc->randr_crtc->scanout_pixmap) {
            ppriv = msGetPixmapPriv(drmmode, crtc->randr_crtc->scanout_pixmap);
            drmModeRmFB(drmmode->fd, ppriv->fb_id);
        }
        if (drmmode_crtc->slave_damage) {
            DamageUnregister(drmmode_crtc->slave_damage);
            drmmode_crtc->slave_damage = NULL;
        }
        return TRUE;
    }

    ppriv = msGetPixmapPriv(drmmode, ppix);
    if (!drmmode_crtc->slave_damage) {
        drmmode_crtc->slave_damage = DamageCreate(NULL, NULL,
                                                  DamageReportNone,
                                                  TRUE,
                                                  crtc->randr_crtc->pScreen,
                                                  NULL);
    }
    ptr = drmmode_map_slave_bo(drmmode, ppriv);
    ppix->devPrivate.ptr = ptr;
    DamageRegister(&ppix->drawable, drmmode_crtc->slave_damage);

    if (ppriv->fb_id == 0) {
        drmModeAddFB(drmmode->fd, ppix->drawable.width,
                     ppix->drawable.height,
                     ppix->drawable.depth,
                     ppix->drawable.bitsPerPixel,
                     ppix->devKind, ppriv->backing_bo->handle, &ppriv->fb_id);
    }
    return TRUE;
}

static void *
drmmode_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int ret;

    if (!drmmode_create_bo(drmmode, &drmmode_crtc->rotate_bo,
                           width, height, crtc->scrn->bitsPerPixel)) {
        xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
               "Couldn't allocate shadow memory for rotated CRTC\n");
        return NULL;
    }

    ret = drmModeAddFB(drmmode->fd, width, height, crtc->scrn->depth,
                       crtc->scrn->bitsPerPixel,
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
                                                 scrn->bitsPerPixel,
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
        drmmode_set_pixmap_bo(drmmode, rotate_pixmap, NULL);
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
    .load_cursor_argb = drmmode_load_cursor_argb,

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

static void
drmmode_crtc_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
    xf86CrtcPtr crtc;
    drmmode_crtc_private_ptr drmmode_crtc;

    crtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
    if (crtc == NULL)
        return;

    drmmode_crtc = xnfcalloc(sizeof(drmmode_crtc_private_rec), 1);
    drmmode_crtc->mode_crtc =
        drmModeGetCrtc(drmmode->fd, drmmode->mode_res->crtcs[num]);
    drmmode_crtc->drmmode = drmmode;
    drmmode_crtc->vblank_pipe = drmmode_crtc_vblank_pipe(num);
    crtc->driver_private = drmmode_crtc;
}

static xf86OutputStatus
drmmode_output_detect(xf86OutputPtr output)
{
    /* go to the hw and retrieve a new output struct */
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    xf86OutputStatus status;

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
    for (i = 0; i < drmmode_output->mode_output->count_encoders; i++) {
        drmModeFreeEncoder(drmmode_output->mode_encoders[i]);
    }
    free(drmmode_output->mode_encoders);
    drmModeFreeConnector(drmmode_output->mode_output);
    free(drmmode_output);
    output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;

    if (!koutput)
        return;

    drmModeConnectorSetProperty(drmmode->fd, koutput->connector_id,
                                drmmode_output->dpms_enum_id, mode);
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
    "DVI",
    "DVI",
    "DVI",
    "Composite",
    "S-video",
    "LVDS",
    "CTV",
    "DIN",
    "DisplayPort",
    "HDMI",
    "HDMI",
    "TV",
    "eDP",
    "Virtual",
    "DSI",
};

static void
drmmode_output_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
    xf86OutputPtr output;
    drmModeConnectorPtr koutput;
    drmModeEncoderPtr *kencoders = NULL;
    drmmode_output_private_ptr drmmode_output;
    drmModePropertyPtr props;
    char name[32];
    int i;

    koutput =
        drmModeGetConnector(drmmode->fd, drmmode->mode_res->connectors[num]);
    if (!koutput)
        return;

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

    /* need to do smart conversion here for compat with non-kms ATI driver */
    if (koutput->connector_type >= MS_ARRAY_SIZE(output_names))
        snprintf(name, 32, "Unknown-%d", koutput->connector_type_id - 1);
    else if (pScrn->is_gpu)
        snprintf(name, 32, "%s-%d-%d", output_names[koutput->connector_type],
                 pScrn->scrnIndex - GPU_SCREEN_OFFSET + 1,
                 koutput->connector_type_id - 1);
    else
        snprintf(name, 32, "%s-%d", output_names[koutput->connector_type],
                 koutput->connector_type_id - 1);

    output = xf86OutputCreate(pScrn, &drmmode_output_funcs, name);
    if (!output) {
        goto out_free_encoders;
    }

    drmmode_output = calloc(sizeof(drmmode_output_private_rec), 1);
    if (!drmmode_output) {
        xf86OutputDestroy(output);
        goto out_free_encoders;
    }

    drmmode_output->output_id = drmmode->mode_res->connectors[num];
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
        output->possible_crtcs &= kencoders[i]->possible_crtcs;
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

    return;
 out_free_encoders:
    if (kencoders) {
        for (i = 0; i < koutput->count_encoders; i++)
            drmModeFreeEncoder(kencoders[i]);
        free(kencoders);
    }
    drmModeFreeConnector(koutput);

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
drmmode_clones_init(ScrnInfoPtr scrn, drmmode_ptr drmmode)
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

            for (k = 0; k < drmmode->mode_res->count_encoders; k++) {
                if (drmmode->mode_res->encoders[k] ==
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

    if (bo == NULL) {
        glamor_egl_destroy_textured_pixmap(pixmap);
        return TRUE;
    }

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

    drmmode_crtc_private_ptr
        drmmode_crtc = xf86_config->crtc[0]->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmmode_bo old_front;
    Bool ret;
    ScreenPtr screen = xf86ScrnToScreen(scrn);
    uint32_t old_fb_id;
    int i, pitch, old_width, old_height, old_pitch;
    int cpp = (scrn->bitsPerPixel + 7) / 8;
    PixmapPtr ppix = screen->GetScreenPixmap(screen);
    void *new_pixels = NULL;

    if (scrn->virtualX == width && scrn->virtualY == height)
        return TRUE;

    xf86DrvMsg(scrn->scrnIndex, X_INFO,
               "Allocate new frame buffer %dx%d stride\n", width, height);

    if (drmmode->triple_buffer_pixmap) {
        screen->DestroyPixmap(drmmode->triple_buffer_pixmap);
        drmmode->triple_buffer_pixmap = NULL;
    }

    old_width = scrn->virtualX;
    old_height = scrn->virtualY;
    old_pitch = drmmode_bo_get_pitch(&drmmode->front_bo);
    old_fb_id = drmmode->fb_id;
    old_front = drmmode->front_bo;

    if (!drmmode_create_bo(drmmode, &drmmode->front_bo,
                           width, height, scrn->bitsPerPixel))
        goto fail;

    pitch = drmmode_bo_get_pitch(&drmmode->front_bo);

    scrn->virtualX = width;
    scrn->virtualY = height;
    scrn->displayWidth = pitch / cpp;

    ret = drmModeAddFB(drmmode->fd, width, height, scrn->depth,
                       scrn->bitsPerPixel, pitch,
                       drmmode_bo_get_handle(&drmmode->front_bo),
                       &drmmode->fb_id);
    if (ret)
        goto fail;

    if (!drmmode->gbm) {
        new_pixels = drmmode_map_front_bo(drmmode);
        if (!new_pixels)
            goto fail;
    }

    if (drmmode->shadow_enable) {
        uint32_t size = scrn->displayWidth * scrn->virtualY *
            ((scrn->bitsPerPixel + 7) >> 3);
        new_pixels = calloc(1, size);
        if (new_pixels == NULL)
            goto fail;
        free(drmmode->shadow_fb);
        drmmode->shadow_fb = new_pixels;
    }

    screen->ModifyPixmapHeader(ppix, width, height, -1, -1, pitch, new_pixels);

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
    scrn->displayWidth = old_pitch / cpp;
    drmmode->fb_id = old_fb_id;

    return FALSE;
}

static const xf86CrtcConfigFuncsRec drmmode_xf86crtc_config_funcs = {
    drmmode_xf86crtc_resize
};

Bool
drmmode_pre_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int cpp)
{
    int i;
    int ret;
    uint64_t value = 0;

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
    drmmode->mode_res = drmModeGetResources(drmmode->fd);
    if (!drmmode->mode_res)
        return FALSE;

    xf86CrtcSetSizeRange(pScrn, 320, 200, drmmode->mode_res->max_width,
                         drmmode->mode_res->max_height);
    for (i = 0; i < drmmode->mode_res->count_crtcs; i++)
        if (!xf86IsEntityShared(pScrn->entityList[0]) ||
            pScrn->confScreen->device->screen == i)
            drmmode_crtc_init(pScrn, drmmode, i);

    for (i = 0; i < drmmode->mode_res->count_connectors; i++)
        drmmode_output_init(pScrn, drmmode, i);

    /* workout clones */
    drmmode_clones_init(pScrn, drmmode);

#if XF86_CRTC_VERSION >= 5
    xf86ProviderSetup(pScrn, NULL, "modesetting");
#endif

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
drmmode_set_desired_modes(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
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
            drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                           0, 0, 0, NULL, 0, NULL);
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

        if (!crtc->funcs->
            set_mode_major(crtc, &crtc->desiredMode, crtc->desiredRotation,
                           crtc->desiredX, crtc->desiredY))
            return FALSE;
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

    dev = udev_monitor_receive_device(drmmode->uevent_monitor);
    if (!dev)
        return;

    RRGetInfo(xf86ScrnToScreen(scrn), TRUE);
    udev_device_unref(dev);
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
    int bpp = pScrn->bitsPerPixel;
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
