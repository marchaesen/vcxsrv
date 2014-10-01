/*
 * Copyright © 2008 Red Hat, Inc.
 * Partly based on code Copyright © 2000 SuSE, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * Red Hat DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL Red Hat
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: Owen Taylor <otaylor@fishsoup.net>
 * Based on code by: Keith Packard
 */

#include <stdlib.h>

#include "glamor_priv.h"

#include <mipict.h>

#if DEBUG_GLYPH_CACHE
#define DBG_GLYPH_CACHE(a) ErrorF a
#else
#define DBG_GLYPH_CACHE(a)
#endif

/* Width of the pixmaps we use for the caches; this should be less than
 * max texture size of the driver; this may need to actually come from
 * the driver.
 */

/* Maximum number of glyphs we buffer on the stack before flushing
 * rendering to the mask or destination surface.
 */
#define GLYPH_BUFFER_SIZE 1024

typedef struct {
    PicturePtr source;
    glamor_composite_rect_t rects[GLYPH_BUFFER_SIZE + 4];
    int count;
} glamor_glyph_buffer_t;

struct glamor_glyph {
    glamor_glyph_cache_t *cache;
    uint16_t x, y;
    uint16_t size, pos;
    unsigned long long left_x1_map, left_x2_map;
    unsigned long long right_x1_map, right_x2_map;      /* Use to check real intersect or not. */
    Bool has_edge_map;
    Bool cached;
};

typedef enum {
    GLAMOR_GLYPH_SUCCESS,       /* Glyph added to render buffer */
    GLAMOR_GLYPH_FAIL,          /* out of memory, etc */
    GLAMOR_GLYPH_NEED_FLUSH,    /* would evict a glyph already in the buffer */
} glamor_glyph_cache_result_t;

#define NeedsComponent(f) (PICT_FORMAT_A(f) != 0 && PICT_FORMAT_RGB(f) != 0)
static DevPrivateKeyRec glamor_glyph_key;

static inline struct glamor_glyph *
glamor_glyph_get_private(ScreenPtr screen, GlyphPtr glyph)
{
    struct glamor_glyph *privates = (struct glamor_glyph*)glyph->devPrivates;

    return &privates[screen->myNum];
}

/*
 * Mask cache is located at the corresponding cache picture's last row.
 * and is deadicated for the mask picture when do the glyphs_via_mask.
 *
 * As we split the glyphs list according to its overlapped or non-overlapped,
 * we can reduce the length of glyphs to do the glyphs_via_mask to 2 or 3
 * glyphs one time for most cases. Thus it give us a case to allocate a
 * small portion of the corresponding cache directly as the mask picture.
 * Then we can rendering the glyphs to this mask picture, and latter we
 * can accumulate the second steps, composite the mask to the dest with
 * the other non-overlapped glyphs's rendering process.
 * Another major benefit is we now only need to clear a relatively small mask
 * region then before. It also make us implement a bunch mask picture clearing
 * algorithm to avoid too frequently small region clearing.
 *
 * If there is no any overlapping, this method will not get performance gain.
 * If there is some overlapping, then this algorithm can get about 15% performance
 * gain.
 */

static void
clear_mask_cache_bitmap(glamor_glyph_mask_cache_t *maskcache,
                        unsigned int clear_mask_bits)
{
    unsigned int i = 0;
    BoxRec box[MASK_CACHE_WIDTH];
    int box_cnt = 0;

    assert((clear_mask_bits & ~MASK_CACHE_MASK) == 0);
    for (i = 0; i < MASK_CACHE_WIDTH; i++) {
        if (clear_mask_bits & (1 << i)) {
            box[box_cnt].x1 = maskcache->mcache[i].x;
            box[box_cnt].x2 = maskcache->mcache[i].x + MASK_CACHE_MAX_SIZE;
            box[box_cnt].y1 = maskcache->mcache[i].y;
            box[box_cnt].y2 = maskcache->mcache[i].y + MASK_CACHE_MAX_SIZE;
            box_cnt++;
        }
    }
    glamor_solid_boxes(maskcache->pixmap, box, box_cnt, 0);
    maskcache->cleared_bitmap |= clear_mask_bits;
}

static void
clear_mask_cache(glamor_glyph_mask_cache_t *maskcache)
{
    int x = 0;
    int cnt = MASK_CACHE_WIDTH;
    unsigned int i = 0;
    struct glamor_glyph_mask_cache_entry *mce;

    glamor_solid(maskcache->pixmap, 0, CACHE_PICTURE_SIZE, CACHE_PICTURE_SIZE,
                 MASK_CACHE_MAX_SIZE, 0);
    mce = &maskcache->mcache[0];
    while (cnt--) {
        mce->width = 0;
        mce->height = 0;
        mce->x = x;
        mce->y = CACHE_PICTURE_SIZE;
        mce->idx = i++;
        x += MASK_CACHE_MAX_SIZE;
        mce++;
    }
    maskcache->free_bitmap = MASK_CACHE_MASK;
    maskcache->cleared_bitmap = MASK_CACHE_MASK;
}

static int
find_continuous_bits(unsigned int bits, int bits_cnt, unsigned int *pbits_mask)
{
    int idx = 0;
    unsigned int bits_mask;

    bits_mask = ((1LL << bits_cnt) - 1);

    if (_X_UNLIKELY(bits_cnt > 56)) {
        while (bits) {
            if ((bits & bits_mask) == bits_mask) {
                *pbits_mask = bits_mask << idx;
                return idx;
            }
            bits >>= 1;
            idx++;
        }
    }
    else {
        idx = __fls(bits);
        while (bits) {
            unsigned int temp_bits;

            temp_bits = bits_mask << (idx - bits_cnt + 1);
            if ((bits & temp_bits) == temp_bits) {
                *pbits_mask = temp_bits;
                return (idx - bits_cnt + 1);
            }
            /* Find first zero. And clear the tested bit. */
            bits &= ~(1LL << idx);
            idx = __fls(~bits);
            bits &= ~((1LL << idx) - 1);
            idx--;
        }
    }
    return -1;
}

static struct glamor_glyph_mask_cache_entry *
get_mask_cache(glamor_glyph_mask_cache_t *maskcache, int blocks)
{
    int free_cleared_bit, idx = -1;
    int retry_cnt = 0;
    unsigned int bits_mask = 0;

    if (maskcache->free_bitmap == 0)
        return NULL;
 retry:
    free_cleared_bit = maskcache->free_bitmap & maskcache->cleared_bitmap;
    if (free_cleared_bit && blocks == 1) {
        idx = __fls(free_cleared_bit);
        bits_mask = 1 << idx;
    }
    else if (free_cleared_bit && blocks > 1) {
        idx = find_continuous_bits(free_cleared_bit, blocks, &bits_mask);
    }

    if (idx < 0) {
        clear_mask_cache_bitmap(maskcache, maskcache->free_bitmap);
        if (retry_cnt++ > 2)
            return NULL;
        goto retry;
    }

    maskcache->cleared_bitmap &= ~bits_mask;
    maskcache->free_bitmap &= ~bits_mask;
    DEBUGF("get idx %d free %x clear %x \n",
           idx, maskcache->free_bitmap, maskcache->cleared_bitmap);
    return &maskcache->mcache[idx];
}

static void
put_mask_cache_bitmap(glamor_glyph_mask_cache_t *maskcache,
                      unsigned int bitmap)
{
    maskcache->free_bitmap |= bitmap;
    DEBUGF("put bitmap %x free %x clear %x \n",
           bitmap, maskcache->free_bitmap, maskcache->cleared_bitmap);
}

static void
glamor_unrealize_glyph_caches(ScreenPtr pScreen)
{
    glamor_screen_private *glamor = glamor_get_screen_private(pScreen);
    int i;

    if (!glamor->glyph_caches_realized)
        return;

    for (i = 0; i < GLAMOR_NUM_GLYPH_CACHE_FORMATS; i++) {
        glamor_glyph_cache_t *cache = &glamor->glyphCaches[i];

        if (cache->picture)
            FreePicture(cache->picture, 0);

        if (cache->glyphs)
            free(cache->glyphs);

        if (glamor->mask_cache[i])
            free(glamor->mask_cache[i]);
    }
    glamor->glyph_caches_realized = FALSE;
}

void
glamor_glyphs_fini(ScreenPtr pScreen)
{
    glamor_unrealize_glyph_caches(pScreen);
}

/* All caches for a single format share a single pixmap for glyph storage,
 * allowing mixing glyphs of different sizes without paying a penalty
 * for switching between source pixmaps. (Note that for a size of font
 * right at the border between two sizes, we might be switching for almost
 * every glyph.)
 *
 * This function allocates the storage pixmap, and then fills in the
 * rest of the allocated structures for all caches with the given format.
 */

Bool
glamor_realize_glyph_caches(ScreenPtr pScreen)
{
    glamor_screen_private *glamor = glamor_get_screen_private(pScreen);

    unsigned int formats[] = {
        PIXMAN_a8,
        PIXMAN_a8r8g8b8,
    };
    int i;

    if (glamor->glyph_caches_realized)
        return TRUE;

    memset(glamor->glyphCaches, 0, sizeof(glamor->glyphCaches));

    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        glamor_glyph_cache_t *cache = &glamor->glyphCaches[i];
        PixmapPtr pixmap;
        PicturePtr picture;
        XID component_alpha;
        int depth = PIXMAN_FORMAT_DEPTH(formats[i]);
        int error;
        PictFormatPtr pPictFormat =
            PictureMatchFormat(pScreen, depth, formats[i]);
        if (!pPictFormat)
            goto bail;

        /* Now allocate the pixmap and picture */
        pixmap = pScreen->CreatePixmap(pScreen,
                                       CACHE_PICTURE_SIZE,
                                       CACHE_PICTURE_SIZE + MASK_CACHE_MAX_SIZE,
                                       depth, GLAMOR_CREATE_NO_LARGE);
        if (!pixmap)
            goto bail;

        component_alpha = NeedsComponent(pPictFormat->format);
        picture = CreatePicture(0, &pixmap->drawable, pPictFormat,
                                CPComponentAlpha, &component_alpha,
                                serverClient, &error);

        pScreen->DestroyPixmap(pixmap);
        if (!picture)
            goto bail;

        ValidatePicture(picture);

        cache->picture = picture;
        cache->glyphs = calloc(sizeof(GlyphPtr), GLYPH_CACHE_SIZE);
        if (!cache->glyphs)
            goto bail;

        cache->evict = rand() % GLYPH_CACHE_SIZE;
        glamor->mask_cache[i] = calloc(1, sizeof(*glamor->mask_cache[i]));
        glamor->mask_cache[i]->pixmap = pixmap;
        clear_mask_cache(glamor->mask_cache[i]);
    }
    assert(i == GLAMOR_NUM_GLYPH_CACHE_FORMATS);

    glamor->glyph_caches_realized = TRUE;
    return TRUE;

 bail:
    glamor_unrealize_glyph_caches(pScreen);
    return FALSE;
}

/**
 * Called by glamor_create_screen_resources() to set up the glyph cache.
 *
 * This was previously required to be called by the drivers, but not
 * as of the xserver 1.16 ABI.
 */
Bool
glamor_glyphs_init(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey(&glamor_glyph_key,
                               PRIVATE_GLYPH,
                               screenInfo.numScreens * sizeof(struct glamor_glyph)))
        return FALSE;

    return TRUE;
}

/* The most efficient thing to way to upload the glyph to the screen
 * is to use CopyArea; glamor pixmaps are always offscreen.
 */
static void
glamor_glyph_cache_upload_glyph(ScreenPtr screen,
                                glamor_glyph_cache_t *cache,
                                GlyphPtr glyph, int x, int y)
{
    PicturePtr pGlyphPicture = GlyphPicture(glyph)[screen->myNum];
    PixmapPtr pGlyphPixmap = (PixmapPtr) pGlyphPicture->pDrawable;
    PixmapPtr pCachePixmap = (PixmapPtr) cache->picture->pDrawable;
    PixmapPtr scratch;
    BoxRec box;
    GCPtr gc;

    gc = GetScratchGC(pCachePixmap->drawable.depth, screen);
    if (!gc)
        return;

    ValidateGC(&pCachePixmap->drawable, gc);

    scratch = pGlyphPixmap;
    if (pGlyphPixmap->drawable.depth != pCachePixmap->drawable.depth) {

        scratch = glamor_create_pixmap(screen,
                                       glyph->info.width,
                                       glyph->info.height,
                                       pCachePixmap->drawable.depth, 0);
        if (scratch) {
            PicturePtr picture;
            int error;

            picture =
                CreatePicture(0,
                              &scratch->drawable,
                              PictureMatchFormat
                              (screen,
                               pCachePixmap->drawable.depth,
                               cache->picture->format),
                              0, NULL, serverClient, &error);
            if (picture) {
                ValidatePicture(picture);
                glamor_composite(PictOpSrc,
                                 pGlyphPicture,
                                 NULL, picture,
                                 0, 0, 0, 0, 0,
                                 0, glyph->info.width, glyph->info.height);
                FreePicture(picture, 0);
            }
        }
        else {
            scratch = pGlyphPixmap;
        }
    }

    box.x1 = x;
    box.y1 = y;
    box.x2 = x + glyph->info.width;
    box.y2 = y + glyph->info.height;
    glamor_copy(&scratch->drawable,
                &pCachePixmap->drawable, NULL,
                &box, 1, -x, -y, FALSE, FALSE, 0, NULL);
    if (scratch != pGlyphPixmap)
        screen->DestroyPixmap(scratch);

    FreeScratchGC(gc);
}

void
glamor_glyph_unrealize(ScreenPtr screen, GlyphPtr glyph)
{
    struct glamor_glyph *priv;

    /* Use Lookup in case we have not attached to this glyph. */
    priv = glamor_glyph_get_private(screen, glyph);

    if (priv->cached)
        priv->cache->glyphs[priv->pos] = NULL;
}

/* Cut and paste from render/glyph.c - probably should export it instead */
static void
glamor_glyph_extents(int nlist,
                     GlyphListPtr list, GlyphPtr *glyphs, BoxPtr extents)
{
    int x1, x2, y1, y2;
    int x, y, n;

    x1 = y1 = MAXSHORT;
    x2 = y2 = MINSHORT;
    x = y = 0;
    while (nlist--) {
        x += list->xOff;
        y += list->yOff;
        n = list->len;
        list++;
        while (n--) {
            GlyphPtr glyph = *glyphs++;
            int v;

            v = x - glyph->info.x;
            if (v < x1)
                x1 = v;
            v += glyph->info.width;
            if (v > x2)
                x2 = v;

            v = y - glyph->info.y;
            if (v < y1)
                y1 = v;
            v += glyph->info.height;
            if (v > y2)
                y2 = v;

            x += glyph->info.xOff;
            y += glyph->info.yOff;
        }
    }

    extents->x1 = x1 < MINSHORT ? MINSHORT : x1;
    extents->x2 = x2 > MAXSHORT ? MAXSHORT : x2;
    extents->y1 = y1 < MINSHORT ? MINSHORT : y1;
    extents->y2 = y2 > MAXSHORT ? MAXSHORT : y2;
}

static void
glamor_glyph_priv_get_edge_map(GlyphPtr glyph, struct glamor_glyph *priv,
                               PicturePtr glyph_picture)
{
    PixmapPtr glyph_pixmap = (PixmapPtr) glyph_picture->pDrawable;
    int j;
    unsigned long long left_x1_map = 0, left_x2_map = 0;
    unsigned long long right_x1_map = 0, right_x2_map = 0;
    int bitsPerPixel;
    int stride;
    void *bits;
    int width;
    unsigned int left_x1_data = 0, left_x2_data = 0;
    unsigned int right_x1_data = 0, right_x2_data = 0;

    bitsPerPixel = glyph_pixmap->drawable.bitsPerPixel;
    stride = glyph_pixmap->devKind;
    bits = glyph_pixmap->devPrivate.ptr;
    width = glyph->info.width;

    if (glyph_pixmap->drawable.width < 2
        || !(glyph_pixmap->drawable.depth == 8
             || glyph_pixmap->drawable.depth == 1
             || glyph_pixmap->drawable.depth == 32)) {
        priv->has_edge_map = FALSE;
        return;
    }

    left_x1_map = left_x2_map = 0;
    right_x1_map = right_x2_map = 0;

    for (j = 0; j < glyph_pixmap->drawable.height; j++) {
        if (bitsPerPixel == 8) {
            unsigned char *data;

            data = (unsigned char *) ((unsigned char *) bits + stride * j);
            left_x1_data = *data++;
            left_x2_data = *data;
            data =
                (unsigned char *) ((unsigned char *) bits + stride * j + width -
                                   2);
            right_x1_data = *data++;
            right_x2_data = *data;
        }
        else if (bitsPerPixel == 32) {
            left_x1_data = *((unsigned int *) bits + stride / 4 * j);
            left_x2_data = *((unsigned int *) bits + stride / 4 * j + 1);
            right_x1_data =
                *((unsigned int *) bits + stride / 4 * j + width - 2);
            right_x2_data =
                *((unsigned int *) bits + stride / 4 * j + width - 1);
        }
        else if (bitsPerPixel == 1) {
            unsigned char temp;

            temp = *((unsigned char *) glyph_pixmap->devPrivate.ptr
                     + glyph_pixmap->devKind * j) & 0x3;
            left_x1_data = temp & 0x1;
            left_x2_data = temp & 0x2;

            temp = *((unsigned char *) glyph_pixmap->devPrivate.ptr
                     + glyph_pixmap->devKind * j
                     + (glyph_pixmap->drawable.width - 2) / 8);
            right_x1_data = temp
                & (1 << ((glyph_pixmap->drawable.width - 2) % 8));
            temp = *((unsigned char *) glyph_pixmap->devPrivate.ptr
                     + glyph_pixmap->devKind * j
                     + (glyph_pixmap->drawable.width - 1) / 8);
            right_x2_data = temp
                & (1 << ((glyph_pixmap->drawable.width - 1) % 8));
        }
        left_x1_map |= (left_x1_data != 0) << j;
        left_x2_map |= (left_x2_data != 0) << j;
        right_x1_map |= (right_x1_data != 0) << j;
        right_x2_map |= (right_x2_data != 0) << j;
    }

    priv->left_x1_map = left_x1_map;
    priv->left_x2_map = left_x2_map;
    priv->right_x1_map = right_x1_map;
    priv->right_x2_map = right_x2_map;
    priv->has_edge_map = TRUE;
    return;
}

/**
 * Returns TRUE if the glyphs in the lists intersect.  Only checks based on
 * bounding box, which appears to be good enough to catch most cases at least.
 */

#define INTERSECTED_TYPE_MASK 1
#define NON_INTERSECTED 0
#define INTERSECTED 1

struct glamor_glyph_list {
    int nlist;
    GlyphListPtr list;
    GlyphPtr *glyphs;
    int type;
};

static Bool
glyph_new_fixed_list(struct glamor_glyph_list *fixed_list,
                     GlyphPtr *cur_glyphs,
                     GlyphPtr ** head_glyphs,
                     GlyphListPtr cur_list,
                     int cur_pos, int cur_x, int cur_y,
                     int x1, int y1, int x2, int y2,
                     GlyphListPtr *head_list,
                     int *head_pos,
                     int *head_x,
                     int *head_y, int *fixed_cnt, int type, BoxPtr prev_extents)
{
    int x_off = 0;
    int y_off = 0;
    int n_off = 0;
    int list_cnt;

    if (type == NON_INTERSECTED) {
        if (x1 < prev_extents->x2 && x2 > prev_extents->x1
            && y1 < prev_extents->y2 && y2 > prev_extents->y1)
            return FALSE;
        x_off = (*(cur_glyphs - 1))->info.xOff;
        y_off = (*(cur_glyphs - 1))->info.yOff;
        n_off = 1;
    }

    list_cnt = cur_list - *head_list + 1;
    if (cur_pos <= n_off) {
        DEBUGF("break at %d n_off %d\n", cur_pos, n_off);
        list_cnt--;
        if (cur_pos < n_off) {
            /* we overlap with previous list's last glyph. */
            x_off += cur_list->xOff;
            y_off += cur_list->yOff;
            cur_list--;
            cur_pos = cur_list->len;
            if (cur_pos <= n_off) {
                list_cnt--;
            }
        }
    }
    DEBUGF("got %d lists\n", list_cnt);
    if (list_cnt != 0) {
        fixed_list->list = malloc(list_cnt * sizeof(*cur_list));
        memcpy(fixed_list->list, *head_list, list_cnt * sizeof(*cur_list));
        fixed_list->list[0].xOff = *head_x;
        fixed_list->list[0].yOff = *head_y;
        fixed_list->glyphs = *head_glyphs;
        fixed_list->type = type & INTERSECTED_TYPE_MASK;
        fixed_list->nlist = list_cnt;
        if (cur_list != *head_list) {
            fixed_list->list[0].len = (*head_list)->len - *head_pos;
            if (cur_pos != n_off)
                fixed_list->list[list_cnt - 1].len = cur_pos - n_off;
        }
        else
            fixed_list->list[0].len = cur_pos - *head_pos - n_off;
        (*fixed_cnt)++;
    }

    if (type <= INTERSECTED) {
        *head_list = cur_list;
        *head_pos = cur_pos - n_off;
        *head_x = cur_x - x_off;
        *head_y = cur_y - y_off;
        *head_glyphs = cur_glyphs - n_off;
    }
    return TRUE;
}

/*
 * This function detects glyph lists's overlapping.
 *
 * If check_fake_overlap is set, then it will check the glyph's left
 * and right small boxes's real overlapping pixels. And if there is
 * no real pixel overlapping, then it will not be treated as overlapped
 * case. And we also can configured it to ignore less than 2 pixels
 * overlappig.
 *
 * This function analyzes all the lists and split the list to multiple
 * lists which are pure overlapped glyph lists or pure non-overlapped
 * list if the overlapping only ocurr on the two adjacent glyphs.
 * Otherwise, it return -1.
 *
 **/

static int
glamor_glyphs_intersect(int nlist, GlyphListPtr list, GlyphPtr *glyphs,
                        PictFormatShort mask_format,
                        ScreenPtr screen, Bool check_fake_overlap,
                        struct glamor_glyph_list *fixed_list, int fixed_size)
{
    int x1, x2, y1, y2;
    int n;
    int x, y;
    BoxPtr extents;
    BoxRec prev_extents;
    Bool first = TRUE, first_list = TRUE;
    Bool need_free_list_region = FALSE;
    Bool need_free_fixed_list = FALSE;
    struct glamor_glyph *priv = NULL;
    Bool in_non_intersected_list = -1;
    GlyphListPtr head_list;
    int head_x, head_y, head_pos;
    int fixed_cnt = 0;
    GlyphPtr *head_glyphs;
    GlyphListPtr cur_list = list;
    RegionRec list_region;
    RegionRec current_region;
    BoxRec current_box;

    if (nlist > 1) {
        pixman_region_init(&list_region);
        need_free_list_region = TRUE;
    }

    pixman_region_init(&current_region);

    extents = pixman_region_extents(&current_region);

    x = 0;
    y = 0;
    x1 = x2 = y1 = y2 = 0;
    n = 0;
    extents->x1 = 0;
    extents->y1 = 0;
    extents->x2 = 0;
    extents->y2 = 0;

    head_list = list;
    DEBUGF("has %d lists.\n", nlist);
    while (nlist--) {
        BoxRec left_box, right_box = { 0 };
        Bool has_left_edge_box = FALSE, has_right_edge_box = FALSE;
        Bool left_to_right;
        struct glamor_glyph *left_priv = NULL, *right_priv = NULL;

        x += list->xOff;
        y += list->yOff;
        n = list->len;
        left_to_right = TRUE;
        cur_list = list++;

        if (_X_UNLIKELY(!first_list)) {
            pixman_region_init_with_extents(&current_region, extents);
            pixman_region_union(&list_region, &list_region, &current_region);
            first = TRUE;
        }
        else {
            head_list = cur_list;
            head_pos = cur_list->len - n;
            head_x = x;
            head_y = y;
            head_glyphs = glyphs;
        }

        DEBUGF("current list %p has %d glyphs\n", cur_list, n);
        while (n--) {
            GlyphPtr glyph = *glyphs++;

            DEBUGF("the %dth glyph\n", cur_list->len - n - 1);
            if (glyph->info.width == 0 || glyph->info.height == 0) {
                x += glyph->info.xOff;
                y += glyph->info.yOff;
                continue;
            }
            if (mask_format
                && mask_format != GlyphPicture(glyph)[screen->myNum]->format) {
                need_free_fixed_list = TRUE;
                goto done;
            }

            x1 = x - glyph->info.x;
            if (x1 < MINSHORT)
                x1 = MINSHORT;
            y1 = y - glyph->info.y;
            if (y1 < MINSHORT)
                y1 = MINSHORT;
            if (check_fake_overlap)
                priv = glamor_glyph_get_private(screen, glyph);

            x2 = x1 + glyph->info.width;
            y2 = y1 + glyph->info.height;

            if (x2 > MAXSHORT)
                x2 = MAXSHORT;
            if (y2 > MAXSHORT)
                y2 = MAXSHORT;

            if (first) {
                extents->x1 = x1;
                extents->y1 = y1;
                extents->x2 = x2;
                extents->y2 = y2;

                prev_extents = *extents;

                first = FALSE;
                if (check_fake_overlap && priv
                    && priv->has_edge_map && glyph->info.yOff == 0) {
                    left_box.x1 = x1;
                    left_box.x2 = x1 + 1;
                    left_box.y1 = y1;

                    right_box.x1 = x2 - 2;
                    right_box.x2 = x2 - 1;
                    right_box.y1 = y1;
                    left_priv = right_priv = priv;
                    has_left_edge_box = TRUE;
                    has_right_edge_box = TRUE;
                }
            }
            else {
                if (_X_UNLIKELY(!first_list)) {
                    current_box.x1 = x1;
                    current_box.y1 = y1;
                    current_box.x2 = x2;
                    current_box.y2 = y2;
                    if (pixman_region_contains_rectangle
                        (&list_region, &current_box) != PIXMAN_REGION_OUT) {
                        need_free_fixed_list = TRUE;
                        goto done;
                    }
                }

                if (x1 < extents->x2 && x2 > extents->x1
                    && y1 < extents->y2 && y2 > extents->y1) {

                    if (check_fake_overlap &&
                        (has_left_edge_box || has_right_edge_box)
                        && priv->has_edge_map && glyph->info.yOff == 0) {
                        int left_dx, right_dx;
                        unsigned long long intersected;

                        left_dx = has_left_edge_box ? 1 : 0;
                        right_dx = has_right_edge_box ? 1 : 0;
                        if (x1 + 1 < extents->x2 - right_dx &&
                            x2 - 1 > extents->x1 + left_dx)
                            goto real_intersected;

                        if (left_to_right && has_right_edge_box) {
                            if (x1 == right_box.x1) {
                                intersected =
                                    ((priv->left_x1_map & right_priv->
                                      right_x1_map)
                                     | (priv->left_x2_map & right_priv->
                                        right_x2_map));
                                if (intersected)
                                    goto real_intersected;
                            }
                            else if (x1 == right_box.x2) {
                                intersected =
                                    (priv->left_x1_map & right_priv->
                                     right_x2_map);
                                if (intersected) {
#ifdef  GLYPHS_EDEGE_OVERLAP_LOOSE_CHECK
                                    /* tolerate with two pixels overlap. */
                                    intersected &= ~(1 << __fls(intersected));
                                    if ((intersected & (intersected - 1)))
#endif
                                        goto real_intersected;
                                }
                            }
                        }
                        else if (!left_to_right && has_left_edge_box) {
                            if (x2 - 1 == left_box.x1) {
                                intersected =
                                    (priv->right_x2_map & left_priv->
                                     left_x1_map);
                                if (intersected) {
#ifdef  GLYPHS_EDEGE_OVERLAP_LOOSE_CHECK
                                    /* tolerate with two pixels overlap. */
                                    intersected &= ~(1 << __fls(intersected));
                                    if ((intersected & (intersected - 1)))
#endif
                                        goto real_intersected;
                                }
                            }
                            else if (x2 - 1 == right_box.x2) {
                                if ((priv->right_x1_map & left_priv->
                                     left_x1_map)
                                    || (priv->right_x2_map & left_priv->
                                        left_x2_map))
                                    goto real_intersected;
                            }
                        }
                        else {
                            if (x1 < extents->x2 && x1 + 2 > extents->x1)
                                goto real_intersected;
                        }
                        goto non_intersected;
                    }
                    else {
 real_intersected:
                        DEBUGF("overlap with previous glyph.\n");
                        if (in_non_intersected_list == 1) {
                            if (fixed_cnt >= fixed_size) {
                                need_free_fixed_list = TRUE;
                                goto done;
                            }
                            if (!glyph_new_fixed_list(&fixed_list[fixed_cnt],
                                                      glyphs - 1,
                                                      &head_glyphs,
                                                      cur_list,
                                                      cur_list->len - (n + 1),
                                                      x, y, x1, y1, x2, y2,
                                                      &head_list, &head_pos,
                                                      &head_x, &head_y,
                                                      &fixed_cnt,
                                                      NON_INTERSECTED,
                                                      &prev_extents)) {
                                need_free_fixed_list = TRUE;
                                goto done;
                            }
                        }

                        in_non_intersected_list = 0;

                    }
                }
                else {
 non_intersected:
                    DEBUGF("doesn't overlap with previous glyph.\n");
                    if (in_non_intersected_list == 0) {
                        if (fixed_cnt >= fixed_size) {
                            need_free_fixed_list = TRUE;
                            goto done;
                        }
                        if (!glyph_new_fixed_list(&fixed_list[fixed_cnt],
                                                  glyphs - 1,
                                                  &head_glyphs,
                                                  cur_list,
                                                  cur_list->len - (n + 1), x, y,
                                                  x1, y1, x2, y2,
                                                  &head_list,
                                                  &head_pos,
                                                  &head_x,
                                                  &head_y, &fixed_cnt,
                                                  INTERSECTED, &prev_extents)) {
                            need_free_fixed_list = TRUE;
                            goto done;
                        }
                    }
                    in_non_intersected_list = 1;
                }
                prev_extents = *extents;
            }

            if (check_fake_overlap && priv
                && priv->has_edge_map && glyph->info.yOff == 0) {
                if (!has_left_edge_box || x1 < extents->x1) {
                    left_box.x1 = x1;
                    left_box.x2 = x1 + 1;
                    left_box.y1 = y1;
                    has_left_edge_box = TRUE;
                    left_priv = priv;
                }

                if (!has_right_edge_box || x2 > extents->x2) {
                    right_box.x1 = x2 - 2;
                    right_box.x2 = x2 - 1;
                    right_box.y1 = y1;
                    has_right_edge_box = TRUE;
                    right_priv = priv;
                }
            }

            if (x1 < extents->x1)
                extents->x1 = x1;
            if (x2 > extents->x2)
                extents->x2 = x2;

            if (y1 < extents->y1)
                extents->y1 = y1;
            if (y2 > extents->y2)
                extents->y2 = y2;

            x += glyph->info.xOff;
            y += glyph->info.yOff;
        }
        first_list = FALSE;
    }

    if (in_non_intersected_list == 0 && fixed_cnt == 0) {
        fixed_cnt = -1;
        goto done;
    }

    if ((in_non_intersected_list != -1 || head_pos != n) && (fixed_cnt > 0)) {
        if (fixed_cnt >= fixed_size) {
            need_free_fixed_list = TRUE;
            goto done;
        }
        if (!glyph_new_fixed_list(&fixed_list[fixed_cnt],
                                  glyphs - 1,
                                  &head_glyphs,
                                  cur_list,
                                  cur_list->len - (n + 1), x, y,
                                  x1, y1, x2, y2,
                                  &head_list,
                                  &head_pos,
                                  &head_x,
                                  &head_y, &fixed_cnt,
                                  (!in_non_intersected_list) | 0x80,
                                  &prev_extents)) {
            need_free_fixed_list = TRUE;
            goto done;
        }
    }

 done:
    if (need_free_list_region)
        pixman_region_fini(&list_region);
    pixman_region_fini(&current_region);

    if (need_free_fixed_list && fixed_cnt >= 0) {
        while (fixed_cnt--) {
            free(fixed_list[fixed_cnt].list);
        }
    }

    DEBUGF("Got %d fixed list \n", fixed_cnt);
    return fixed_cnt;
}

static inline unsigned int
glamor_glyph_size_to_count(int size)
{
    size /= GLYPH_MIN_SIZE;
    return size * size;
}

static inline unsigned int
glamor_glyph_count_to_mask(int count)
{
    return ~(count - 1);
}

static inline unsigned int
glamor_glyph_size_to_mask(int size)
{
    return glamor_glyph_count_to_mask(glamor_glyph_size_to_count(size));
}

static PicturePtr
glamor_glyph_cache(glamor_screen_private *glamor, GlyphPtr glyph, int *out_x,
                   int *out_y)
{
    ScreenPtr screen = glamor->screen;
    PicturePtr glyph_picture = GlyphPicture(glyph)[screen->myNum];
    glamor_glyph_cache_t *cache =
        &glamor->glyphCaches[PICT_FORMAT_RGB(glyph_picture->format) != 0];
    struct glamor_glyph *priv = NULL, *evicted_priv = NULL;
    int size, mask, pos, s;

    if (glyph->info.width > GLYPH_MAX_SIZE
        || glyph->info.height > GLYPH_MAX_SIZE)
        return NULL;

    for (size = GLYPH_MIN_SIZE; size <= GLYPH_MAX_SIZE; size *= 2)
        if (glyph->info.width <= size && glyph->info.height <= size)
            break;

    s = glamor_glyph_size_to_count(size);
    mask = glamor_glyph_count_to_mask(s);
    pos = (cache->count + s - 1) & mask;

    priv = glamor_glyph_get_private(screen, glyph);
    if (pos < GLYPH_CACHE_SIZE) {
        cache->count = pos + s;
    }
    else {
        for (s = size; s <= GLYPH_MAX_SIZE; s *= 2) {
            int i = cache->evict & glamor_glyph_size_to_mask(s);
            GlyphPtr evicted = cache->glyphs[i];

            if (evicted == NULL)
                continue;

            evicted_priv = glamor_glyph_get_private(screen, evicted);
            assert(evicted_priv->pos == i);
            if (evicted_priv->size >= s) {
                cache->glyphs[i] = NULL;
                evicted_priv->cached = FALSE;
                pos = cache->evict & glamor_glyph_size_to_mask(size);
            }
            else
                evicted_priv = NULL;
            break;
        }
        if (evicted_priv == NULL) {
            int count = glamor_glyph_size_to_count(size);

            mask = glamor_glyph_count_to_mask(count);
            pos = cache->evict & mask;
            for (s = 0; s < count; s++) {
                GlyphPtr evicted = cache->glyphs[pos + s];

                if (evicted != NULL) {

                    evicted_priv = glamor_glyph_get_private(screen, evicted);

                    assert(evicted_priv->pos == pos + s);
                    evicted_priv->cached = FALSE;
                    cache->glyphs[pos + s] = NULL;
                }
            }

        }
        /* And pick a new eviction position */
        cache->evict = rand() % GLYPH_CACHE_SIZE;
    }

    cache->glyphs[pos] = glyph;

    priv->cache = cache;
    priv->size = size;
    priv->pos = pos;
    s = pos / ((GLYPH_MAX_SIZE / GLYPH_MIN_SIZE) *
               (GLYPH_MAX_SIZE / GLYPH_MIN_SIZE));
    priv->x = s % (CACHE_PICTURE_SIZE / GLYPH_MAX_SIZE) * GLYPH_MAX_SIZE;
    priv->y = (s / (CACHE_PICTURE_SIZE / GLYPH_MAX_SIZE)) * GLYPH_MAX_SIZE;
    for (s = GLYPH_MIN_SIZE; s < GLYPH_MAX_SIZE; s *= 2) {
        if (pos & 1)
            priv->x += s;
        if (pos & 2)
            priv->y += s;
        pos >>= 2;
    }

    glamor_glyph_cache_upload_glyph(screen, cache, glyph, priv->x, priv->y);
#ifndef GLYPHS_NO_EDEGEMAP_OVERLAP_CHECK
    if (priv->has_edge_map == FALSE && glyph->info.width >= 2)
        glamor_glyph_priv_get_edge_map(glyph, priv, glyph_picture);
#endif
    priv->cached = TRUE;

    *out_x = priv->x;
    *out_y = priv->y;
    return cache->picture;
}

typedef void (*glyphs_flush_func) (void *arg);
struct glyphs_flush_dst_arg {
    CARD8 op;
    PicturePtr src;
    PicturePtr dst;
    glamor_glyph_buffer_t *buffer;
    int x_src, y_src;
    int x_dst, y_dst;
};

static struct glyphs_flush_dst_arg dst_arg;
static struct glyphs_flush_mask_arg mask_arg;
static glamor_glyph_buffer_t dst_buffer;
static glamor_glyph_buffer_t mask_buffer;
unsigned long long mask_glyphs_cnt = 0;
unsigned long long dst_glyphs_cnt = 0;

#define GLYPHS_DST_MODE_VIA_MASK		0
#define GLYPHS_DST_MODE_VIA_MASK_CACHE		1
#define GLYPHS_DST_MODE_TO_DST			2
#define GLYPHS_DST_MODE_MASK_TO_DST		3

struct glyphs_flush_mask_arg {
    PicturePtr mask;
    glamor_glyph_buffer_t *buffer;
    glamor_glyph_mask_cache_t *maskcache;
    unsigned int used_bitmap;
};

static void
glamor_glyphs_flush_mask(struct glyphs_flush_mask_arg *arg)
{
    if (arg->buffer->count > 0) {
#ifdef RENDER
        glamor_composite_glyph_rects(PictOpAdd, arg->buffer->source,
                                     NULL, arg->mask,
                                     arg->buffer->count, arg->buffer->rects);
#endif
    }
    arg->buffer->count = 0;
    arg->buffer->source = NULL;

}

static void
glamor_glyphs_flush_dst(struct glyphs_flush_dst_arg *arg)
{
    if (!arg->buffer)
        return;

    if (mask_buffer.count > 0) {
        glamor_glyphs_flush_mask(&mask_arg);
    }
    if (mask_arg.used_bitmap) {
        put_mask_cache_bitmap(mask_arg.maskcache, mask_arg.used_bitmap);
        mask_arg.used_bitmap = 0;
    }

    if (arg->buffer->count > 0) {
        glamor_composite_glyph_rects(arg->op, arg->src,
                                     arg->buffer->source, arg->dst,
                                     arg->buffer->count,
                                     &arg->buffer->rects[0]);
        arg->buffer->count = 0;
        arg->buffer->source = NULL;
    }
}

static glamor_glyph_cache_result_t
glamor_buffer_glyph(glamor_screen_private *glamor_priv,
                    glamor_glyph_buffer_t *buffer,
                    PictFormatShort format,
                    GlyphPtr glyph, struct glamor_glyph *priv,
                    int x_glyph, int y_glyph,
                    int dx, int dy, int w, int h,
                    int glyphs_dst_mode,
                    glyphs_flush_func glyphs_flush, void *flush_arg)
{
    ScreenPtr screen = glamor_priv->screen;
    glamor_composite_rect_t *rect;
    PicturePtr source;
    int x, y;
    glamor_glyph_cache_t *cache;

    if (glyphs_dst_mode != GLYPHS_DST_MODE_MASK_TO_DST)
        priv = glamor_glyph_get_private(screen, glyph);

    if (PICT_FORMAT_BPP(format) == 1)
        format = PICT_a8;

    cache = &glamor_priv->glyphCaches[PICT_FORMAT_RGB(format) != 0];

    if (buffer->source && buffer->source != cache->picture && glyphs_flush) {
        (*glyphs_flush) (flush_arg);
        glyphs_flush = NULL;
    }

    if (buffer->count == GLYPH_BUFFER_SIZE && glyphs_flush) {
        (*glyphs_flush) (flush_arg);
        glyphs_flush = NULL;
    }

    if (priv && priv->cached) {
        rect = &buffer->rects[buffer->count++];
        rect->x_src = priv->x + dx;
        rect->y_src = priv->y + dy;
        if (buffer->source == NULL)
            buffer->source = priv->cache->picture;
        if (glyphs_dst_mode <= GLYPHS_DST_MODE_VIA_MASK_CACHE)
            assert(priv->cache->glyphs[priv->pos] == glyph);
    }
    else {
        assert(glyphs_dst_mode != GLYPHS_DST_MODE_MASK_TO_DST);
        if (glyphs_flush)
            (*glyphs_flush) (flush_arg);
        source = glamor_glyph_cache(glamor_priv, glyph, &x, &y);

        if (source != NULL) {
            rect = &buffer->rects[buffer->count++];
            rect->x_src = x + dx;
            rect->y_src = y + dy;
            if (buffer->source == NULL)
                buffer->source = source;
            if (glyphs_dst_mode == GLYPHS_DST_MODE_VIA_MASK_CACHE) {
                /* mode 1 means we are using global mask cache,
                 * thus we have to composite from the cache picture
                 * to the cache picture, we need a flush here to make
                 * sure latter we get the corret glyphs data.*/
                glamor_make_current(glamor_priv);
                glFlush();
            }
        }
        else {
            /* Couldn't find the glyph in the cache, use the glyph picture directly */
            source = GlyphPicture(glyph)[screen->myNum];
            if (buffer->source && buffer->source != source && glyphs_flush)
                (*glyphs_flush) (flush_arg);
            buffer->source = source;

            rect = &buffer->rects[buffer->count++];
            rect->x_src = 0 + dx;
            rect->y_src = 0 + dy;
        }
        priv = glamor_glyph_get_private(screen, glyph);
    }

    rect->x_dst = x_glyph;
    rect->y_dst = y_glyph;
    if (glyphs_dst_mode != GLYPHS_DST_MODE_MASK_TO_DST) {
        rect->x_dst -= glyph->info.x;
        rect->y_dst -= glyph->info.y;
    }
    rect->width = w;
    rect->height = h;
    if (glyphs_dst_mode > GLYPHS_DST_MODE_VIA_MASK_CACHE) {
        rect->x_mask = rect->x_src;
        rect->y_mask = rect->y_src;
        rect->x_src = dst_arg.x_src + rect->x_dst - dst_arg.x_dst;
        rect->y_src = dst_arg.y_src + rect->y_dst - dst_arg.y_dst;
    }

    return GLAMOR_GLYPH_SUCCESS;
}

static void
glamor_buffer_glyph_clip(glamor_screen_private *glamor_priv,
                         BoxPtr rects,
                         int nrect, PictFormatShort format,
                         GlyphPtr glyph, struct glamor_glyph *priv,
                         int glyph_x, int glyph_y,
                         int glyph_dx, int glyph_dy,
                         int width, int height,
                         int glyphs_mode,
                         glyphs_flush_func flush_func, void *arg)
{
    int i;

    for (i = 0; i < nrect; i++) {
        int dst_x, dst_y;
        int dx, dy;
        int x2, y2;

        dst_x = glyph_x - glyph_dx;
        dst_y = glyph_y - glyph_dy;
        x2 = dst_x + width;
        y2 = dst_y + height;
        dx = dy = 0;
        if (rects[i].y1 >= y2)
            break;

        if (dst_x < rects[i].x1)
            dx = rects[i].x1 - dst_x, dst_x = rects[i].x1;
        if (x2 > rects[i].x2)
            x2 = rects[i].x2;
        if (dst_y < rects[i].y1)
            dy = rects[i].y1 - dst_y, dst_y = rects[i].y1;
        if (y2 > rects[i].y2)
            y2 = rects[i].y2;
        if (dst_x < x2 && dst_y < y2) {

            glamor_buffer_glyph(glamor_priv,
                                &dst_buffer,
                                format,
                                glyph, priv,
                                dst_x + glyph_dx,
                                dst_y + glyph_dy,
                                dx, dy,
                                x2 - dst_x, y2 - dst_y,
                                glyphs_mode, flush_func, arg);
        }
    }
}

static void
glamor_glyphs_via_mask(CARD8 op,
                       PicturePtr src,
                       PicturePtr dst,
                       PictFormatPtr mask_format,
                       INT16 x_src,
                       INT16 y_src,
                       int nlist, GlyphListPtr list, GlyphPtr *glyphs,
                       Bool use_mask_cache)
{
    PixmapPtr mask_pixmap = 0;
    PicturePtr mask;
    ScreenPtr screen = dst->pDrawable->pScreen;
    int width = 0, height = 0;
    int x, y;
    int x_dst = list->xOff, y_dst = list->yOff;
    int n;
    GlyphPtr glyph;
    int error;
    BoxRec extents = { 0, 0, 0, 0 };
    XID component_alpha;
    glamor_screen_private *glamor_priv;
    int need_free_mask = FALSE;
    glamor_glyph_buffer_t buffer;
    struct glyphs_flush_mask_arg arg;
    glamor_glyph_buffer_t *pmask_buffer;
    struct glyphs_flush_mask_arg *pmask_arg;
    struct glamor_glyph_mask_cache_entry *mce = NULL;
    glamor_glyph_mask_cache_t *maskcache;
    glamor_glyph_cache_t *cache;
    int glyphs_dst_mode;

    glamor_glyph_extents(nlist, list, glyphs, &extents);

    if (extents.x2 <= extents.x1 || extents.y2 <= extents.y1)
        return;
    glamor_priv = glamor_get_screen_private(screen);
    width = extents.x2 - extents.x1;
    height = extents.y2 - extents.y1;

    if (mask_format->depth == 1) {
        PictFormatPtr a8Format = PictureMatchFormat(screen, 8, PICT_a8);

        if (a8Format)
            mask_format = a8Format;
    }

    cache = &glamor_priv->glyphCaches
        [PICT_FORMAT_RGB(mask_format->format) != 0];
    maskcache = glamor_priv->mask_cache[PICT_FORMAT_RGB(mask_format->format) != 0];

    x = -extents.x1;
    y = -extents.y1;
    if (!use_mask_cache || width > (CACHE_PICTURE_SIZE / 4)
        || height > MASK_CACHE_MAX_SIZE) {
 new_mask_pixmap:
        mask_pixmap = glamor_create_pixmap(screen, width, height,
                                           mask_format->depth,
                                           CREATE_PIXMAP_USAGE_SCRATCH);
        if (!mask_pixmap) {
            glamor_destroy_pixmap(mask_pixmap);
            return;
        }
        glamor_solid(mask_pixmap, 0, 0, width, height, 0);
        component_alpha = NeedsComponent(mask_format->format);
        mask = CreatePicture(0, &mask_pixmap->drawable,
                             mask_format, CPComponentAlpha,
                             &component_alpha, serverClient, &error);
        if (!mask)
            return;
        need_free_mask = TRUE;
        pmask_arg = &arg;
        pmask_buffer = &buffer;
        pmask_buffer->count = 0;
        pmask_buffer->source = NULL;
        pmask_arg->used_bitmap = 0;
        glyphs_dst_mode = GLYPHS_DST_MODE_VIA_MASK;
    }
    else {
        int retry_cnt = 0;

 retry:
        mce = get_mask_cache(maskcache,
                             (width + MASK_CACHE_MAX_SIZE -
                              1) / MASK_CACHE_MAX_SIZE);

        if (mce == NULL) {
            glamor_glyphs_flush_dst(&dst_arg);
            retry_cnt++;
            if (retry_cnt > 2) {
                assert(0);
                goto new_mask_pixmap;
            }
            goto retry;
        }

        mask = cache->picture;
        x += mce->x;
        y += mce->y;
        mce->width = (width + MASK_CACHE_MAX_SIZE - 1) / MASK_CACHE_MAX_SIZE;
        mce->height = 1;
        if (mask_arg.mask && mask_arg.mask != mask && mask_buffer.count != 0)
            glamor_glyphs_flush_dst(&dst_arg);
        pmask_arg = &mask_arg;
        pmask_buffer = &mask_buffer;
        pmask_arg->maskcache = maskcache;
        glyphs_dst_mode = GLYPHS_DST_MODE_VIA_MASK_CACHE;
    }
    pmask_arg->mask = mask;
    pmask_arg->buffer = pmask_buffer;
    while (nlist--) {
        x += list->xOff;
        y += list->yOff;
        n = list->len;
        mask_glyphs_cnt += n;
        while (n--) {
            glyph = *glyphs++;
            if (glyph->info.width > 0 && glyph->info.height > 0) {
                glyphs_flush_func flush_func;
                void *temp_arg;

                if (need_free_mask) {
                    if (pmask_buffer->count)
                        flush_func =
                            (glyphs_flush_func) glamor_glyphs_flush_mask;
                    else
                        flush_func = NULL;
                    temp_arg = pmask_arg;
                }
                else {
                    /* If we are using global mask cache, then we need to
                     * flush dst instead of mask. As some dst depends on the
                     * previous mask result. Just flush mask can't get all previous's
                     * overlapped glyphs.*/
                    if (dst_buffer.count || mask_buffer.count)
                        flush_func =
                            (glyphs_flush_func) glamor_glyphs_flush_dst;
                    else
                        flush_func = NULL;
                    temp_arg = &dst_arg;
                }
                glamor_buffer_glyph(glamor_priv, pmask_buffer,
                                    mask_format->format,
                                    glyph, NULL, x, y,
                                    0, 0,
                                    glyph->info.width, glyph->info.height,
                                    glyphs_dst_mode,
                                    flush_func, (void *) temp_arg);
            }
            x += glyph->info.xOff;
            y += glyph->info.yOff;
        }
        list++;
    }

    x = extents.x1;
    y = extents.y1;
    if (need_free_mask) {
        glamor_glyphs_flush_mask(pmask_arg);
        CompositePicture(op,
                         src,
                         mask,
                         dst,
                         x_src + x - x_dst,
                         y_src + y - y_dst, 0, 0, x, y, width, height);
        FreePicture(mask, 0);
        glamor_destroy_pixmap(mask_pixmap);
    }
    else {
        struct glamor_glyph priv;
        glyphs_flush_func flush_func;
        BoxPtr rects;
        int nrect;

        priv.cache = cache;
        priv.x = mce->x;
        priv.y = mce->y;
        priv.cached = TRUE;
        rects = REGION_RECTS(dst->pCompositeClip);
        nrect = REGION_NUM_RECTS(dst->pCompositeClip);

        pmask_arg->used_bitmap |= ((1 << mce->width) - 1) << mce->idx;
        dst_arg.op = op;
        dst_arg.src = src;
        dst_arg.dst = dst;
        dst_arg.buffer = &dst_buffer;
        dst_arg.x_src = x_src;
        dst_arg.y_src = y_src;
        dst_arg.x_dst = x_dst;
        dst_arg.y_dst = y_dst;

        if (dst_buffer.source == NULL) {
            dst_buffer.source = cache->picture;
        }
        else if (dst_buffer.source != cache->picture) {
            glamor_glyphs_flush_dst(&dst_arg);
            dst_buffer.source = cache->picture;
        }

        x += dst->pDrawable->x;
        y += dst->pDrawable->y;

        if (dst_buffer.count || mask_buffer.count)
            flush_func = (glyphs_flush_func) glamor_glyphs_flush_dst;
        else
            flush_func = NULL;

        glamor_buffer_glyph_clip(glamor_priv,
                                 rects, nrect,
                                 mask_format->format,
                                 NULL, &priv,
                                 x, y,
                                 0, 0,
                                 width, height,
                                 GLYPHS_DST_MODE_MASK_TO_DST,
                                 flush_func, (void *) &dst_arg);
    }
}

static void
glamor_glyphs_to_dst(CARD8 op,
                     PicturePtr src,
                     PicturePtr dst,
                     INT16 x_src,
                     INT16 y_src,
                     int nlist, GlyphListPtr list, GlyphPtr *glyphs)
{
    ScreenPtr screen = dst->pDrawable->pScreen;
    int x = 0, y = 0;
    int x_dst = list->xOff, y_dst = list->yOff;
    int n;
    GlyphPtr glyph;
    BoxPtr rects;
    int nrect;
    glamor_screen_private *glamor_priv;

    rects = REGION_RECTS(dst->pCompositeClip);
    nrect = REGION_NUM_RECTS(dst->pCompositeClip);

    glamor_priv = glamor_get_screen_private(screen);

    dst_arg.op = op;
    dst_arg.src = src;
    dst_arg.dst = dst;
    dst_arg.buffer = &dst_buffer;
    dst_arg.x_src = x_src;
    dst_arg.y_src = y_src;
    dst_arg.x_dst = x_dst;
    dst_arg.y_dst = y_dst;

    x = dst->pDrawable->x;
    y = dst->pDrawable->y;

    while (nlist--) {
        x += list->xOff;
        y += list->yOff;
        n = list->len;
        dst_glyphs_cnt += n;
        while (n--) {
            glyph = *glyphs++;

            if (glyph->info.width > 0 && glyph->info.height > 0) {
                glyphs_flush_func flush_func;

                if (dst_buffer.count || mask_buffer.count)
                    flush_func = (glyphs_flush_func) glamor_glyphs_flush_dst;
                else
                    flush_func = NULL;
                glamor_buffer_glyph_clip(glamor_priv,
                                         rects, nrect,
                                         (GlyphPicture(glyph)[screen->myNum])->
                                         format, glyph, NULL, x, y,
                                         glyph->info.x, glyph->info.y,
                                         glyph->info.width, glyph->info.height,
                                         GLYPHS_DST_MODE_TO_DST, flush_func,
                                         (void *) &dst_arg);
            }

            x += glyph->info.xOff;
            y += glyph->info.yOff;
        }
        list++;
    }
}

#define MAX_FIXED_SIZE
static void
glamor_glyphs_reset_buffer(glamor_glyph_buffer_t *buffer)
{
    buffer->count = 0;
    buffer->source = NULL;
}

static Bool
_glamor_glyphs(CARD8 op,
               PicturePtr src,
               PicturePtr dst,
               PictFormatPtr mask_format,
               INT16 x_src,
               INT16 y_src, int nlist, GlyphListPtr list,
               GlyphPtr *glyphs, Bool fallback)
{
    PictFormatShort format;
    int fixed_size, fixed_cnt = 0;
    struct glamor_glyph_list *fixed_list = NULL;
    Bool need_free_list = FALSE;

#ifndef GLYPHS_NO_EDEGEMAP_OVERLAP_CHECK
    Bool check_fake_overlap = TRUE;

    if (!(op == PictOpOver || op == PictOpAdd || op == PictOpXor)) {
        /* C = (0,0,0,0) D = glyphs , SRC = A, DEST = B (faked overlapped glyphs, overlapped with (0,0,0,0)).
         * For those op, (A IN (C ADD D)) OP B !=  (A IN D) OP ((A IN C) OP B)
         *              or (A IN (D ADD C)) OP B != (A IN C) OP ((A IN D) OP B)
         * We need to split the faked regions to three or two, and composite the disoverlapped small
         * boxes one by one. For other Ops, it's safe to composite the whole box.  */
        check_fake_overlap = FALSE;
    }
#else
    Bool check_fake_overlap = FALSE;
#endif
    if (mask_format)
        format = mask_format->depth << 24 | mask_format->format;
    else
        format = 0;

    fixed_size = 32;
    glamor_glyphs_reset_buffer(&dst_buffer);

    if (!mask_format || (((nlist == 1 && list->len == 1) || op == PictOpAdd)
                         && (dst->format ==
                             ((mask_format->depth << 24) | mask_format->
                              format)))) {
        glamor_glyphs_to_dst(op, src, dst, x_src, y_src, nlist, list, glyphs);
        goto last_flush;
    }

    glamor_glyphs_reset_buffer(&mask_buffer);

    /* We have mask_format. Need to check the real overlap or not. */
    format = mask_format->depth << 24 | mask_format->format;

    fixed_list = calloc(fixed_size, sizeof(*fixed_list));
    if (_X_UNLIKELY(fixed_list == NULL))
        fixed_size = 0;
    fixed_cnt = glamor_glyphs_intersect(nlist, list, glyphs,
                                        format, dst->pDrawable->pScreen,
                                        check_fake_overlap,
                                        fixed_list, fixed_size);
    if (fixed_cnt == 0)
        mask_format = NULL;
    need_free_list = TRUE;

    if (fixed_cnt <= 0) {
        if (mask_format == NULL) {
            glamor_glyphs_to_dst(op, src, dst, x_src, y_src, nlist,
                                 list, glyphs);
            goto last_flush;
        }
        else {
            glamor_glyphs_via_mask(op, src, dst, mask_format,
                                   x_src, y_src, nlist, list, glyphs, FALSE);
            goto free_fixed_list;
        }
    }
    else {

        /* We have splitted the original list to serval list, some are overlapped
         * and some are non-overlapped. For the non-overlapped, we render it to
         * dst directly. For the overlapped, we render it to mask picture firstly,
         * then render the mask to dst. If we can use mask cache which is in the
         * glyphs cache's last row, we can accumulate the rendering of mask to dst
         * with the other dst_buffer's rendering operations thus can reduce the call
         * of glDrawElements.
         *
         * */
        struct glamor_glyph_list *saved_list;

        saved_list = fixed_list;
        mask_arg.used_bitmap = 0;
        while (fixed_cnt--) {
            if (fixed_list->type == NON_INTERSECTED) {
                glamor_glyphs_to_dst(op, src, dst,
                                     x_src, y_src,
                                     fixed_list->nlist,
                                     fixed_list->list, fixed_list->glyphs);
            }
            else
                glamor_glyphs_via_mask(op, src, dst,
                                       mask_format, x_src, y_src,
                                       fixed_list->nlist,
                                       fixed_list->list,
                                       fixed_list->glyphs, TRUE);

            free(fixed_list->list);
            fixed_list++;
        }
        free(saved_list);
        need_free_list = FALSE;
    }

 last_flush:
    if (dst_buffer.count || mask_buffer.count)
        glamor_glyphs_flush_dst(&dst_arg);
 free_fixed_list:
    if (need_free_list) {
        assert(fixed_cnt <= 0);
        free(fixed_list);
    }
    return TRUE;
}

void
glamor_glyphs(CARD8 op,
              PicturePtr src,
              PicturePtr dst,
              PictFormatPtr mask_format,
              INT16 x_src,
              INT16 y_src, int nlist, GlyphListPtr list, GlyphPtr *glyphs)
{
    _glamor_glyphs(op, src, dst, mask_format, x_src,
                   y_src, nlist, list, glyphs, TRUE);
}

Bool
glamor_glyphs_nf(CARD8 op,
                 PicturePtr src,
                 PicturePtr dst,
                 PictFormatPtr mask_format,
                 INT16 x_src,
                 INT16 y_src, int nlist, GlyphListPtr list, GlyphPtr *glyphs)
{
    return _glamor_glyphs(op, src, dst, mask_format, x_src,
                          y_src, nlist, list, glyphs, FALSE);
}
