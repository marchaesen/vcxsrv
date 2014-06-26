/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
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
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include <stdlib.h>

#include "glamor_priv.h"

#define GLAMOR_CACHE_EXPIRE_MAX 100

#define GLAMOR_CACHE_DEFAULT    0
#define GLAMOR_CACHE_EXACT_SIZE 1

//#define NO_FBO_CACHE 1
#define FBO_CACHE_THRESHOLD  (256*1024*1024)

/* Loop from the tail to the head. */
#define xorg_list_for_each_entry_reverse(pos, head, member)             \
    for (pos = __container_of((head)->prev, pos, member);               \
         &pos->member != (head);                                        \
         pos = __container_of(pos->member.prev, pos, member))

#define xorg_list_for_each_entry_safe_reverse(pos, tmp, head, member)   \
    for (pos = __container_of((head)->prev, pos, member),               \
         tmp = __container_of(pos->member.prev, pos, member);           \
         &pos->member != (head);                                        \
         pos = tmp, tmp = __container_of(pos->member.prev, tmp, member))

inline static int
cache_wbucket(int size)
{
    int order = __fls(size / 32);

    if (order >= CACHE_BUCKET_WCOUNT)
        order = CACHE_BUCKET_WCOUNT - 1;
    return order;
}

inline static int
cache_hbucket(int size)
{
    int order = __fls(size / 32);

    if (order >= CACHE_BUCKET_HCOUNT)
        order = CACHE_BUCKET_HCOUNT - 1;
    return order;
}

static glamor_pixmap_fbo *
glamor_pixmap_fbo_cache_get(glamor_screen_private *glamor_priv,
                            int w, int h, GLenum format, int flag)
{
    struct xorg_list *cache;
    glamor_pixmap_fbo *fbo_entry, *ret_fbo = NULL;
    int n_format;

#ifdef NO_FBO_CACHE
    return NULL;
#else
    n_format = cache_format(format);
    if (n_format == -1)
        return NULL;
    cache = &glamor_priv->fbo_cache[n_format]
        [cache_wbucket(w)]
        [cache_hbucket(h)];
    if (!(flag & GLAMOR_CACHE_EXACT_SIZE)) {
        xorg_list_for_each_entry(fbo_entry, cache, list) {
            if (fbo_entry->width >= w && fbo_entry->height >= h) {

                DEBUGF("Request w %d h %d format %x \n", w, h, format);
                DEBUGF("got cache entry %p w %d h %d fbo %d tex %d format %x\n",
                       fbo_entry, fbo_entry->width, fbo_entry->height,
                       fbo_entry->fb, fbo_entry->tex);
                xorg_list_del(&fbo_entry->list);
                ret_fbo = fbo_entry;
                break;
            }
        }
    }
    else {
        xorg_list_for_each_entry(fbo_entry, cache, list) {
            if (fbo_entry->width == w && fbo_entry->height == h) {

                DEBUGF("Request w %d h %d format %x \n", w, h, format);
                DEBUGF("got cache entry %p w %d h %d fbo %d tex %d format %x\n",
                       fbo_entry, fbo_entry->width, fbo_entry->height,
                       fbo_entry->fb, fbo_entry->tex, fbo_entry->format);
                assert(format == fbo_entry->format);
                xorg_list_del(&fbo_entry->list);
                ret_fbo = fbo_entry;
                break;
            }
        }
    }

    if (ret_fbo)
        glamor_priv->fbo_cache_watermark -= ret_fbo->width * ret_fbo->height;

    assert(glamor_priv->fbo_cache_watermark >= 0);

    return ret_fbo;
#endif
}

void
glamor_purge_fbo(glamor_pixmap_fbo *fbo)
{
    glamor_make_current(fbo->glamor_priv);

    if (fbo->fb)
        glDeleteFramebuffers(1, &fbo->fb);
    if (fbo->tex)
        glDeleteTextures(1, &fbo->tex);
    if (fbo->pbo)
        glDeleteBuffers(1, &fbo->pbo);

    free(fbo);
}

static void
glamor_pixmap_fbo_cache_put(glamor_pixmap_fbo *fbo)
{
    struct xorg_list *cache;
    int n_format;

#ifdef NO_FBO_CACHE
    glamor_purge_fbo(fbo);
    return;
#else
    n_format = cache_format(fbo->format);

    if (fbo->fb == 0 || n_format == -1
        || fbo->glamor_priv->fbo_cache_watermark >= FBO_CACHE_THRESHOLD) {
        fbo->glamor_priv->tick += GLAMOR_CACHE_EXPIRE_MAX;
        glamor_fbo_expire(fbo->glamor_priv);
        glamor_purge_fbo(fbo);
        return;
    }

    cache = &fbo->glamor_priv->fbo_cache[n_format]
        [cache_wbucket(fbo->width)]
        [cache_hbucket(fbo->height)];
    DEBUGF
        ("Put cache entry %p to cache %p w %d h %d format %x fbo %d tex %d \n",
         fbo, cache, fbo->width, fbo->height, fbo->format, fbo->fb, fbo->tex);

    fbo->glamor_priv->fbo_cache_watermark += fbo->width * fbo->height;
    xorg_list_add(&fbo->list, cache);
    fbo->expire = fbo->glamor_priv->tick + GLAMOR_CACHE_EXPIRE_MAX;
#endif
}

static int
glamor_pixmap_ensure_fb(glamor_pixmap_fbo *fbo)
{
    int status, err = 0;

    glamor_make_current(fbo->glamor_priv);

    if (fbo->fb == 0)
        glGenFramebuffers(1, &fbo->fb);
    assert(fbo->tex != 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fbo->tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        const char *str;

        switch (status) {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            str = "incomplete attachment";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            str = "incomplete/missing attachment";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            str = "incomplete draw buffer";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            str = "incomplete read buffer";
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            str = "unsupported";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            str = "incomplete multiple";
            break;
        default:
            str = "unknown error";
            break;
        }

        glamor_fallback("glamor: Failed to create fbo, %s\n", str);
        err = -1;
    }

    return err;
}

glamor_pixmap_fbo *
glamor_create_fbo_from_tex(glamor_screen_private *glamor_priv,
                           int w, int h, GLenum format, GLint tex, int flag)
{
    glamor_pixmap_fbo *fbo;

    fbo = calloc(1, sizeof(*fbo));
    if (fbo == NULL)
        return NULL;

    xorg_list_init(&fbo->list);

    fbo->tex = tex;
    fbo->width = w;
    fbo->height = h;
    fbo->format = format;
    fbo->glamor_priv = glamor_priv;

    if (flag == GLAMOR_CREATE_PIXMAP_MAP) {
        glamor_make_current(glamor_priv);
        glGenBuffers(1, &fbo->pbo);
        goto done;
    }

    if (flag != GLAMOR_CREATE_FBO_NO_FBO) {
        if (glamor_pixmap_ensure_fb(fbo) != 0) {
            glamor_purge_fbo(fbo);
            fbo = NULL;
        }
    }

 done:
    return fbo;
}

void
glamor_fbo_expire(glamor_screen_private *glamor_priv)
{
    struct xorg_list *cache;
    glamor_pixmap_fbo *fbo_entry, *tmp;
    int i, j, k;

    for (i = 0; i < CACHE_FORMAT_COUNT; i++)
        for (j = 0; j < CACHE_BUCKET_WCOUNT; j++)
            for (k = 0; k < CACHE_BUCKET_HCOUNT; k++) {
                cache = &glamor_priv->fbo_cache[i][j][k];
                xorg_list_for_each_entry_safe_reverse(fbo_entry, tmp, cache,
                                                      list) {
                    if (GLAMOR_TICK_AFTER(fbo_entry->expire, glamor_priv->tick)) {
                        break;
                    }

                    glamor_priv->fbo_cache_watermark -=
                        fbo_entry->width * fbo_entry->height;
                    xorg_list_del(&fbo_entry->list);
                    DEBUGF("cache %p fbo %p expired %d current %d \n", cache,
                           fbo_entry, fbo_entry->expire, glamor_priv->tick);
                    glamor_purge_fbo(fbo_entry);
                }
            }

}

void
glamor_init_pixmap_fbo(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    int i, j, k;

    glamor_priv = glamor_get_screen_private(screen);
    for (i = 0; i < CACHE_FORMAT_COUNT; i++)
        for (j = 0; j < CACHE_BUCKET_WCOUNT; j++)
            for (k = 0; k < CACHE_BUCKET_HCOUNT; k++) {
                xorg_list_init(&glamor_priv->fbo_cache[i][j][k]);
            }
    glamor_priv->fbo_cache_watermark = 0;
}

void
glamor_fini_pixmap_fbo(ScreenPtr screen)
{
    struct xorg_list *cache;
    glamor_screen_private *glamor_priv;
    glamor_pixmap_fbo *fbo_entry, *tmp;
    int i, j, k;

    glamor_priv = glamor_get_screen_private(screen);
    for (i = 0; i < CACHE_FORMAT_COUNT; i++)
        for (j = 0; j < CACHE_BUCKET_WCOUNT; j++)
            for (k = 0; k < CACHE_BUCKET_HCOUNT; k++) {
                cache = &glamor_priv->fbo_cache[i][j][k];
                xorg_list_for_each_entry_safe_reverse(fbo_entry, tmp, cache,
                                                      list) {
                    xorg_list_del(&fbo_entry->list);
                    glamor_purge_fbo(fbo_entry);
                }
            }
}

void
glamor_destroy_fbo(glamor_pixmap_fbo *fbo)
{
    xorg_list_del(&fbo->list);
    glamor_pixmap_fbo_cache_put(fbo);

}

static int
_glamor_create_tex(glamor_screen_private *glamor_priv,
                   int w, int h, GLenum format)
{
    unsigned int tex = 0;

    /* With dri3, we want to allocate ARGB8888 pixmaps only.
     * Depending on the implementation, GL_RGBA might not
     * give us ARGB8888. We ask glamor_egl to use get
     * an ARGB8888 based texture for us. */
    if (glamor_priv->dri3_enabled && format == GL_RGBA) {
        tex = glamor_egl_create_argb8888_based_texture(glamor_priv->screen,
                                                       w, h);
    }
    if (!tex) {
        glamor_make_current(glamor_priv);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0,
                     format, GL_UNSIGNED_BYTE, NULL);
    }
    return tex;
}

glamor_pixmap_fbo *
glamor_create_fbo(glamor_screen_private *glamor_priv,
                  int w, int h, GLenum format, int flag)
{
    glamor_pixmap_fbo *fbo;
    GLint tex = 0;
    int cache_flag;

    if (flag == GLAMOR_CREATE_FBO_NO_FBO)
        goto new_fbo;

    if (flag == GLAMOR_CREATE_PIXMAP_MAP)
        goto no_tex;

    /* Tiling from textures requires exact pixmap sizes. As we don't
     * know which pixmaps will be used as tiles, just allocate
     * everything at the requested size
     */
    cache_flag = GLAMOR_CACHE_EXACT_SIZE;

    fbo = glamor_pixmap_fbo_cache_get(glamor_priv, w, h, format, cache_flag);
    if (fbo)
        return fbo;
 new_fbo:
    tex = _glamor_create_tex(glamor_priv, w, h, format);
 no_tex:
    fbo = glamor_create_fbo_from_tex(glamor_priv, w, h, format, tex, flag);

    return fbo;
}

static glamor_pixmap_fbo *
_glamor_create_fbo_array(glamor_screen_private *glamor_priv,
                         int w, int h, GLenum format, int flag,
                         int block_w, int block_h,
                         glamor_pixmap_private *pixmap_priv, int has_fbo)
{
    int block_wcnt;
    int block_hcnt;
    glamor_pixmap_fbo **fbo_array;
    BoxPtr box_array;
    int i, j;
    glamor_pixmap_private_large_t *priv;

    priv = &pixmap_priv->large;

    block_wcnt = (w + block_w - 1) / block_w;
    block_hcnt = (h + block_h - 1) / block_h;

    box_array = calloc(block_wcnt * block_hcnt, sizeof(box_array[0]));
    if (box_array == NULL)
        return NULL;

    fbo_array = calloc(block_wcnt * block_hcnt, sizeof(glamor_pixmap_fbo *));
    if (fbo_array == NULL) {
        free(box_array);
        return FALSE;
    }
    for (i = 0; i < block_hcnt; i++) {
        int block_y1, block_y2;
        int fbo_w, fbo_h;

        block_y1 = i * block_h;
        block_y2 = (block_y1 + block_h) > h ? h : (block_y1 + block_h);
        fbo_h = block_y2 - block_y1;

        for (j = 0; j < block_wcnt; j++) {
            box_array[i * block_wcnt + j].x1 = j * block_w;
            box_array[i * block_wcnt + j].y1 = block_y1;
            box_array[i * block_wcnt + j].x2 =
                (j + 1) * block_w > w ? w : (j + 1) * block_w;
            box_array[i * block_wcnt + j].y2 = block_y2;
            fbo_w =
                box_array[i * block_wcnt + j].x2 - box_array[i * block_wcnt +
                                                             j].x1;
            if (!has_fbo)
                fbo_array[i * block_wcnt + j] = glamor_create_fbo(glamor_priv,
                                                                  fbo_w, fbo_h,
                                                                  format,
                                                                  GLAMOR_CREATE_PIXMAP_FIXUP);
            else
                fbo_array[i * block_wcnt + j] = priv->base.fbo;
            if (fbo_array[i * block_wcnt + j] == NULL)
                goto cleanup;
        }
    }

    priv->box = box_array[0];
    priv->box_array = box_array;
    priv->fbo_array = fbo_array;
    priv->block_wcnt = block_wcnt;
    priv->block_hcnt = block_hcnt;
    return fbo_array[0];

 cleanup:
    for (i = 0; i < block_wcnt * block_hcnt; i++)
        if ((fbo_array)[i])
            glamor_destroy_fbo((fbo_array)[i]);
    free(box_array);
    free(fbo_array);
    return NULL;
}

/* Create a fbo array to cover the w*h region, by using block_w*block_h
 * block.*/
glamor_pixmap_fbo *
glamor_create_fbo_array(glamor_screen_private *glamor_priv,
                        int w, int h, GLenum format, int flag,
                        int block_w, int block_h,
                        glamor_pixmap_private *pixmap_priv)
{
    pixmap_priv->large.block_w = block_w;
    pixmap_priv->large.block_h = block_h;
    return _glamor_create_fbo_array(glamor_priv, w, h, format, flag,
                                    block_w, block_h, pixmap_priv, 0);
}

glamor_pixmap_fbo *
glamor_pixmap_detach_fbo(glamor_pixmap_private *pixmap_priv)
{
    glamor_pixmap_fbo *fbo;

    if (pixmap_priv == NULL)
        return NULL;

    fbo = pixmap_priv->base.fbo;
    if (fbo == NULL)
        return NULL;

    pixmap_priv->base.fbo = NULL;
    return fbo;
}

/* The pixmap must not be attached to another fbo. */
void
glamor_pixmap_attach_fbo(PixmapPtr pixmap, glamor_pixmap_fbo *fbo)
{
    glamor_pixmap_private *pixmap_priv;

    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (pixmap_priv->base.fbo)
        return;

    pixmap_priv->base.fbo = fbo;

    switch (pixmap_priv->type) {
    case GLAMOR_TEXTURE_LARGE:
    case GLAMOR_TEXTURE_ONLY:
    case GLAMOR_TEXTURE_DRM:
        pixmap_priv->base.gl_fbo = GLAMOR_FBO_NORMAL;
        if (fbo->tex != 0)
            pixmap_priv->base.gl_tex = 1;
        else {
            /* XXX For the Xephyr only, may be broken now. */
            pixmap_priv->base.gl_tex = 0;
        }
    case GLAMOR_MEMORY_MAP:
        pixmap->devPrivate.ptr = NULL;
        break;
    default:
        break;
    }
}

void
glamor_pixmap_destroy_fbo(glamor_pixmap_private *priv)
{
    glamor_pixmap_fbo *fbo;

    if (priv->type == GLAMOR_TEXTURE_LARGE) {
        int i;
        glamor_pixmap_private_large_t *large = &priv->large;

        for (i = 0; i < large->block_wcnt * large->block_hcnt; i++)
            glamor_destroy_fbo(large->fbo_array[i]);
        free(large->fbo_array);
    }
    else {
        fbo = glamor_pixmap_detach_fbo(priv);
        if (fbo)
            glamor_destroy_fbo(fbo);
    }

    free(priv);
}

Bool
glamor_pixmap_ensure_fbo(PixmapPtr pixmap, GLenum format, int flag)
{
    glamor_screen_private *glamor_priv;
    glamor_pixmap_private *pixmap_priv;
    glamor_pixmap_fbo *fbo;

    glamor_priv = glamor_get_screen_private(pixmap->drawable.pScreen);
    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (pixmap_priv->base.fbo == NULL) {

        fbo = glamor_create_fbo(glamor_priv, pixmap->drawable.width,
                                pixmap->drawable.height, format, flag);
        if (fbo == NULL)
            return FALSE;

        glamor_pixmap_attach_fbo(pixmap, fbo);
    }
    else {
        /* We do have a fbo, but it may lack of fb or tex. */
        if (!pixmap_priv->base.fbo->tex)
            pixmap_priv->base.fbo->tex =
                _glamor_create_tex(glamor_priv, pixmap->drawable.width,
                                   pixmap->drawable.height, format);

        if (flag != GLAMOR_CREATE_FBO_NO_FBO && pixmap_priv->base.fbo->fb == 0)
            if (glamor_pixmap_ensure_fb(pixmap_priv->base.fbo) != 0)
                return FALSE;
    }

    return TRUE;
}

_X_EXPORT void
glamor_pixmap_exchange_fbos(PixmapPtr front, PixmapPtr back)
{
    glamor_pixmap_private *front_priv, *back_priv;
    glamor_pixmap_fbo *temp_fbo;

    front_priv = glamor_get_pixmap_private(front);
    back_priv = glamor_get_pixmap_private(back);
    temp_fbo = front_priv->base.fbo;
    front_priv->base.fbo = back_priv->base.fbo;
    back_priv->base.fbo = temp_fbo;
}
