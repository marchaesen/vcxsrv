/*
 * Copyright © 2008 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */

/** @file glamor.c
 * This file covers the initialization and teardown of glamor, and has various
 * functions not responsible for performing rendering.
 */

#include <stdlib.h>

#include "glamor_priv.h"

static DevPrivateKeyRec glamor_screen_private_key_index;
DevPrivateKey glamor_screen_private_key = &glamor_screen_private_key_index;
static DevPrivateKeyRec glamor_pixmap_private_key_index;
DevPrivateKey glamor_pixmap_private_key = &glamor_pixmap_private_key_index;

/**
 * glamor_get_drawable_pixmap() returns a backing pixmap for a given drawable.
 *
 * @param drawable the drawable being requested.
 *
 * This function returns the backing pixmap for a drawable, whether it is a
 * redirected window, unredirected window, or already a pixmap.  Note that
 * coordinate translation is needed when drawing to the backing pixmap of a
 * redirected window, and the translation coordinates are provided by calling
 * exaGetOffscreenPixmap() on the drawable.
 */
PixmapPtr
glamor_get_drawable_pixmap(DrawablePtr drawable)
{
    if (drawable->type == DRAWABLE_WINDOW)
        return drawable->pScreen->GetWindowPixmap((WindowPtr) drawable);
    else
        return (PixmapPtr) drawable;
}

_X_EXPORT void
glamor_set_pixmap_type(PixmapPtr pixmap, glamor_pixmap_type_t type)
{
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);

    pixmap_priv = dixLookupPrivate(&pixmap->devPrivates,
                                   glamor_pixmap_private_key);
    if (pixmap_priv == NULL) {
        pixmap_priv = calloc(sizeof(*pixmap_priv), 1);
        glamor_set_pixmap_private(pixmap, pixmap_priv);
        pixmap_priv->base.pixmap = pixmap;
        pixmap_priv->base.glamor_priv = glamor_priv;
    }
    pixmap_priv->type = type;
}

_X_EXPORT void
glamor_set_pixmap_texture(PixmapPtr pixmap, unsigned int tex)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv;
    glamor_pixmap_fbo *fbo;
    GLenum format;

    glamor_priv = glamor_get_screen_private(screen);
    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (pixmap_priv->base.fbo) {
        fbo = glamor_pixmap_detach_fbo(pixmap_priv);
        glamor_destroy_fbo(fbo);
    }

    format = gl_iformat_for_pixmap(pixmap);
    fbo = glamor_create_fbo_from_tex(glamor_priv, pixmap->drawable.width,
                                     pixmap->drawable.height, format, tex, 0);

    if (fbo == NULL) {
        ErrorF("XXX fail to create fbo.\n");
        return;
    }

    glamor_pixmap_attach_fbo(pixmap, fbo);
}

void
glamor_set_screen_pixmap(PixmapPtr screen_pixmap, PixmapPtr *back_pixmap)
{
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv;

    glamor_priv = glamor_get_screen_private(screen_pixmap->drawable.pScreen);
    pixmap_priv = glamor_get_pixmap_private(screen_pixmap);
    glamor_priv->screen_fbo = pixmap_priv->base.fbo->fb;

    pixmap_priv->base.fbo->width = screen_pixmap->drawable.width;
    pixmap_priv->base.fbo->height = screen_pixmap->drawable.height;

    glamor_priv->back_pixmap = back_pixmap;
}

PixmapPtr
glamor_create_pixmap(ScreenPtr screen, int w, int h, int depth,
                     unsigned int usage)
{
    PixmapPtr pixmap;
    glamor_pixmap_type_t type = GLAMOR_TEXTURE_ONLY;
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_fbo *fbo;
    int pitch;
    GLenum format;

    if (w > 32767 || h > 32767)
        return NullPixmap;

    if ((usage == GLAMOR_CREATE_PIXMAP_CPU
         || (usage == CREATE_PIXMAP_USAGE_GLYPH_PICTURE && w <= 64 && h <= 64)
         || (w == 0 && h == 0)
         || !glamor_check_pixmap_fbo_depth(depth))
        || (!GLAMOR_TEXTURED_LARGE_PIXMAP &&
            !glamor_check_fbo_size(glamor_priv, w, h)))
        return fbCreatePixmap(screen, w, h, depth, usage);
    else
        pixmap = fbCreatePixmap(screen, 0, 0, depth, usage);

    pixmap_priv = calloc(1, sizeof(*pixmap_priv));

    if (!pixmap_priv) {
        fbDestroyPixmap(pixmap);
        return fbCreatePixmap(screen, w, h, depth, usage);
    }
    glamor_set_pixmap_private(pixmap, pixmap_priv);

    if (usage == GLAMOR_CREATE_PIXMAP_MAP)
        type = GLAMOR_MEMORY_MAP;

    pixmap_priv->base.pixmap = pixmap;
    pixmap_priv->base.glamor_priv = glamor_priv;

    format = gl_iformat_for_pixmap(pixmap);

    pitch = (((w * pixmap->drawable.bitsPerPixel + 7) / 8) + 3) & ~3;
    screen->ModifyPixmapHeader(pixmap, w, h, 0, 0, pitch, NULL);

    if (type == GLAMOR_MEMORY_MAP || glamor_check_fbo_size(glamor_priv, w, h)) {
        pixmap_priv->type = type;
        fbo = glamor_create_fbo(glamor_priv, w, h, format, usage);
    }
    else {
        DEBUGF("Create LARGE pixmap %p width %d height %d\n", pixmap, w, h);
        pixmap_priv->type = GLAMOR_TEXTURE_LARGE;
        fbo = glamor_create_fbo_array(glamor_priv, w, h, format, usage,
                                      glamor_priv->max_fbo_size,
                                      glamor_priv->max_fbo_size, pixmap_priv);
    }

    if (fbo == NULL) {
        fbDestroyPixmap(pixmap);
        free(pixmap_priv);
        return fbCreatePixmap(screen, w, h, depth, usage);
    }

    glamor_pixmap_attach_fbo(pixmap, fbo);

    return pixmap;
}

void
glamor_destroy_textured_pixmap(PixmapPtr pixmap)
{
    if (pixmap->refcnt == 1) {
        glamor_pixmap_private *pixmap_priv;

        pixmap_priv = glamor_get_pixmap_private(pixmap);
        if (pixmap_priv != NULL)
            glamor_pixmap_destroy_fbo(pixmap_priv);
    }
}

Bool
glamor_destroy_pixmap(PixmapPtr pixmap)
{
    glamor_screen_private
        *glamor_priv = glamor_get_screen_private(pixmap->drawable.pScreen);
    if (glamor_priv->dri3_enabled)
        glamor_egl_destroy_textured_pixmap(pixmap);
    else
        glamor_destroy_textured_pixmap(pixmap);
    return fbDestroyPixmap(pixmap);
}

void
glamor_block_handler(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_get_context(glamor_priv);
    glamor_priv->tick++;
    glFlush();
    glamor_fbo_expire(glamor_priv);
    glamor_put_context(glamor_priv);
    if (glamor_priv->state == RENDER_STATE
        && glamor_priv->render_idle_cnt++ > RENDER_IDEL_MAX) {
        glamor_priv->state = IDLE_STATE;
        glamor_priv->render_idle_cnt = 0;
    }
}

static void
_glamor_block_handler(void *data, OSTimePtr timeout, void *last_select_mask)
{
    glamor_screen_private *glamor_priv = data;

    glamor_get_context(glamor_priv);
    glFlush();
    glamor_put_context(glamor_priv);
}

static void
_glamor_wakeup_handler(void *data, int result, void *last_select_mask)
{
}

static void
glamor_set_debug_level(int *debug_level)
{
    char *debug_level_string;

    debug_level_string = getenv("GLAMOR_DEBUG");
    if (debug_level_string
        && sscanf(debug_level_string, "%d", debug_level) == 1)
        return;
    *debug_level = 0;
}

int glamor_debug_level;


/** Set up glamor for an already-configured GL context. */
Bool
glamor_init(ScreenPtr screen, unsigned int flags)
{
    glamor_screen_private *glamor_priv;
    int gl_version;

#ifdef RENDER
    PictureScreenPtr ps = GetPictureScreenIfSet(screen);
#endif
    if (flags & ~GLAMOR_VALID_FLAGS) {
        ErrorF("glamor_init: Invalid flags %x\n", flags);
        return FALSE;
    }
    glamor_priv = calloc(1, sizeof(*glamor_priv));
    if (glamor_priv == NULL)
        return FALSE;

    if (flags & GLAMOR_INVERTED_Y_AXIS) {
        glamor_priv->yInverted = TRUE;
    }
    else
        glamor_priv->yInverted = FALSE;

    if (!dixRegisterPrivateKey(glamor_screen_private_key, PRIVATE_SCREEN, 0)) {
        LogMessage(X_WARNING,
                   "glamor%d: Failed to allocate screen private\n",
                   screen->myNum);
        goto fail;
    }

    glamor_set_screen_private(screen, glamor_priv);

    if (!dixRegisterPrivateKey(glamor_pixmap_private_key, PRIVATE_PIXMAP, 0)) {
        LogMessage(X_WARNING,
                   "glamor%d: Failed to allocate pixmap private\n",
                   screen->myNum);
        goto fail;;
    }

    if (epoxy_is_desktop_gl())
        glamor_priv->gl_flavor = GLAMOR_GL_DESKTOP;
    else
        glamor_priv->gl_flavor = GLAMOR_GL_ES2;

    gl_version = glamor_gl_get_version();

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
        if (gl_version < GLAMOR_GL_VERSION_ENCODE(1, 3)) {
            ErrorF("Require OpenGL version 1.3 or later.\n");
            goto fail;
        }
    } else {
        if (gl_version < GLAMOR_GL_VERSION_ENCODE(2, 0)) {
            ErrorF("Require Open GLES2.0 or later.\n");
            goto fail;
        }

        if (!glamor_gl_has_extension("GL_EXT_texture_format_BGRA8888")) {
            ErrorF("GL_EXT_texture_format_BGRA8888 required\n");
            goto fail;
        }
    }

    glamor_priv->has_pack_invert =
        glamor_gl_has_extension("GL_MESA_pack_invert");
    glamor_priv->has_fbo_blit =
        glamor_gl_has_extension("GL_EXT_framebuffer_blit");
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &glamor_priv->max_fbo_size);
#ifdef MAX_FBO_SIZE
    glamor_priv->max_fbo_size = MAX_FBO_SIZE;
#endif

    glamor_set_debug_level(&glamor_debug_level);

    /* If we are using egl screen, call egl screen init to
     * register correct close screen function. */
    if (flags & GLAMOR_USE_EGL_SCREEN) {
        glamor_egl_screen_init(screen, &glamor_priv->ctx);
    } else {
        if (!glamor_glx_screen_init(&glamor_priv->ctx))
            goto fail;
    }

    glamor_priv->saved_procs.close_screen = screen->CloseScreen;
    screen->CloseScreen = glamor_close_screen;

    if (flags & GLAMOR_USE_SCREEN) {
        if (!RegisterBlockAndWakeupHandlers(_glamor_block_handler,
                                            _glamor_wakeup_handler,
                                            glamor_priv)) {
            goto fail;
        }

        glamor_priv->saved_procs.create_gc = screen->CreateGC;
        screen->CreateGC = glamor_create_gc;

        glamor_priv->saved_procs.create_pixmap = screen->CreatePixmap;
        screen->CreatePixmap = glamor_create_pixmap;

        glamor_priv->saved_procs.destroy_pixmap = screen->DestroyPixmap;
        screen->DestroyPixmap = glamor_destroy_pixmap;

        glamor_priv->saved_procs.get_spans = screen->GetSpans;
        screen->GetSpans = glamor_get_spans;

        glamor_priv->saved_procs.get_image = screen->GetImage;
        screen->GetImage = glamor_get_image;

        glamor_priv->saved_procs.change_window_attributes =
            screen->ChangeWindowAttributes;
        screen->ChangeWindowAttributes = glamor_change_window_attributes;

        glamor_priv->saved_procs.copy_window = screen->CopyWindow;
        screen->CopyWindow = glamor_copy_window;

        glamor_priv->saved_procs.bitmap_to_region = screen->BitmapToRegion;
        screen->BitmapToRegion = glamor_bitmap_to_region;
    }
#ifdef RENDER
    if (flags & GLAMOR_USE_PICTURE_SCREEN) {
        glamor_priv->saved_procs.composite = ps->Composite;
        ps->Composite = glamor_composite;

        glamor_priv->saved_procs.trapezoids = ps->Trapezoids;
        ps->Trapezoids = glamor_trapezoids;

        glamor_priv->saved_procs.triangles = ps->Triangles;
        ps->Triangles = glamor_triangles;

        glamor_priv->saved_procs.addtraps = ps->AddTraps;
        ps->AddTraps = glamor_add_traps;

    }

    glamor_priv->saved_procs.composite_rects = ps->CompositeRects;
    ps->CompositeRects = glamor_composite_rectangles;

    glamor_priv->saved_procs.glyphs = ps->Glyphs;
    ps->Glyphs = glamor_glyphs;

    glamor_priv->saved_procs.unrealize_glyph = ps->UnrealizeGlyph;
    ps->UnrealizeGlyph = glamor_glyph_unrealize;

    glamor_priv->saved_procs.create_picture = ps->CreatePicture;
    ps->CreatePicture = glamor_create_picture;

    glamor_priv->saved_procs.set_window_pixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = glamor_set_window_pixmap;

    glamor_priv->saved_procs.destroy_picture = ps->DestroyPicture;
    ps->DestroyPicture = glamor_destroy_picture;
    glamor_init_composite_shaders(screen);
#endif
    glamor_init_pixmap_fbo(screen);
    glamor_init_solid_shader(screen);
    glamor_init_tile_shader(screen);
#ifdef GLAMOR_TRAPEZOID_SHADER
    glamor_init_trapezoid_shader(screen);
#endif
    glamor_init_putimage_shaders(screen);
    glamor_init_finish_access_shaders(screen);
#ifdef GLAMOR_GRADIENT_SHADER
    glamor_init_gradient_shader(screen);
#endif
#ifdef GLAMOR_XV
    glamor_init_xv_shader(screen);
#endif
    glamor_pixmap_init(screen);

    glamor_priv->flags = flags;
    glamor_priv->screen = screen;

    return TRUE;

 fail:
    free(glamor_priv);
    glamor_set_screen_private(screen, NULL);
    return FALSE;
}

static void
glamor_release_screen_priv(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;

    glamor_priv = glamor_get_screen_private(screen);
#ifdef GLAMOR_XV
    glamor_fini_xv_shader(screen);
#endif
#ifdef RENDER
    glamor_fini_composite_shaders(screen);
#endif
    glamor_fini_pixmap_fbo(screen);
    glamor_fini_solid_shader(screen);
    glamor_fini_tile_shader(screen);
#ifdef GLAMOR_TRAPEZOID_SHADER
    glamor_fini_trapezoid_shader(screen);
#endif
    glamor_fini_putimage_shaders(screen);
    glamor_fini_finish_access_shaders(screen);
#ifdef GLAMOR_GRADIENT_SHADER
    glamor_fini_gradient_shader(screen);
#endif
    glamor_pixmap_fini(screen);
    free(glamor_priv);

    glamor_set_screen_private(screen, NULL);
}

_X_EXPORT void
glamor_set_pixmap_private(PixmapPtr pixmap, glamor_pixmap_private *priv)
{
    glamor_pixmap_private *old_priv;
    glamor_pixmap_fbo *fbo;

    old_priv = dixGetPrivate(&pixmap->devPrivates, glamor_pixmap_private_key);

    if (priv) {
        assert(old_priv == NULL);
    }
    else {
        if (old_priv == NULL)
            return;
        fbo = glamor_pixmap_detach_fbo(old_priv);
        glamor_purge_fbo(fbo);
        free(old_priv);
    }

    dixSetPrivate(&pixmap->devPrivates, glamor_pixmap_private_key, priv);
}

Bool
glamor_close_screen(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    PixmapPtr screen_pixmap;
    int flags;

#ifdef RENDER
    PictureScreenPtr ps = GetPictureScreenIfSet(screen);
#endif
    glamor_priv = glamor_get_screen_private(screen);
    flags = glamor_priv->flags;
    glamor_glyphs_fini(screen);
    screen->CloseScreen = glamor_priv->saved_procs.close_screen;
    if (flags & GLAMOR_USE_SCREEN) {

        screen->CreateGC = glamor_priv->saved_procs.create_gc;
        screen->CreatePixmap = glamor_priv->saved_procs.create_pixmap;
        screen->DestroyPixmap = glamor_priv->saved_procs.destroy_pixmap;
        screen->GetSpans = glamor_priv->saved_procs.get_spans;
        screen->ChangeWindowAttributes =
            glamor_priv->saved_procs.change_window_attributes;
        screen->CopyWindow = glamor_priv->saved_procs.copy_window;
        screen->BitmapToRegion = glamor_priv->saved_procs.bitmap_to_region;
    }
#ifdef RENDER
    if (ps && (flags & GLAMOR_USE_PICTURE_SCREEN)) {

        ps->Composite = glamor_priv->saved_procs.composite;
        ps->Trapezoids = glamor_priv->saved_procs.trapezoids;
        ps->Triangles = glamor_priv->saved_procs.triangles;
        ps->CreatePicture = glamor_priv->saved_procs.create_picture;
    }
    ps->CompositeRects = glamor_priv->saved_procs.composite_rects;
    ps->Glyphs = glamor_priv->saved_procs.glyphs;
    ps->UnrealizeGlyph = glamor_priv->saved_procs.unrealize_glyph;
    screen->SetWindowPixmap = glamor_priv->saved_procs.set_window_pixmap;
#endif
    screen_pixmap = screen->GetScreenPixmap(screen);
    glamor_set_pixmap_private(screen_pixmap, NULL);
    if (glamor_priv->back_pixmap && *glamor_priv->back_pixmap)
        glamor_set_pixmap_private(*glamor_priv->back_pixmap, NULL);

    glamor_release_screen_priv(screen);

    return screen->CloseScreen(screen);
}

void
glamor_fini(ScreenPtr screen)
{
    /* Do nothing currently. */
}

void
glamor_enable_dri3(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_priv->dri3_enabled = TRUE;
}

Bool
glamor_is_dri3_support_enabled(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    return glamor_priv->dri3_enabled;
}

int
glamor_dri3_fd_from_pixmap(ScreenPtr screen,
                           PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (pixmap_priv == NULL || !glamor_priv->dri3_enabled)
        return -1;
    switch (pixmap_priv->type) {
    case GLAMOR_TEXTURE_DRM:
    case GLAMOR_TEXTURE_ONLY:
        glamor_pixmap_ensure_fbo(pixmap, GL_RGBA, 0);
        return glamor_egl_dri3_fd_name_from_tex(screen,
                                                pixmap,
                                                pixmap_priv->base.fbo->tex,
                                                FALSE, stride, size);
    default:
        break;
    }
    return -1;
}

int
glamor_dri3_name_from_pixmap(PixmapPtr pixmap)
{
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (pixmap_priv == NULL || !glamor_priv->dri3_enabled)
        return -1;
    switch (pixmap_priv->type) {
    case GLAMOR_TEXTURE_DRM:
    case GLAMOR_TEXTURE_ONLY:
        glamor_pixmap_ensure_fbo(pixmap, GL_RGBA, 0);
        return glamor_egl_dri3_fd_name_from_tex(pixmap->drawable.pScreen,
                                                pixmap,
                                                pixmap_priv->base.fbo->tex,
                                                TRUE, NULL, NULL);
    default:
        break;
    }
    return -1;
}
