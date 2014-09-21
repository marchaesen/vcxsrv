/*

Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include "scrnintstr.h"
#include "misc.h"
#include "os.h"
#include "windowstr.h"
#include "resource.h"
#include "dixstruct.h"
#include "gcstruct.h"
#include "servermd.h"
#include "site.h"

/*
 *  Scratch pixmap management and device independent pixmap allocation
 *  function.
 */

/* callable by ddx */
PixmapPtr
GetScratchPixmapHeader(ScreenPtr pScreen, int width, int height, int depth,
                       int bitsPerPixel, int devKind, void *pPixData)
{
    PixmapPtr pPixmap = pScreen->pScratchPixmap;

    if (pPixmap)
        pScreen->pScratchPixmap = NULL;
    else
        /* width and height of 0 means don't allocate any pixmap data */
        pPixmap = (*pScreen->CreatePixmap) (pScreen, 0, 0, depth, 0);

    if (pPixmap) {
        if ((*pScreen->ModifyPixmapHeader) (pPixmap, width, height, depth,
                                            bitsPerPixel, devKind, pPixData))
            return pPixmap;
        (*pScreen->DestroyPixmap) (pPixmap);
    }
    return NullPixmap;
}

/* callable by ddx */
void
FreeScratchPixmapHeader(PixmapPtr pPixmap)
{
    if (pPixmap) {
        ScreenPtr pScreen = pPixmap->drawable.pScreen;

        pPixmap->devPrivate.ptr = NULL; /* lest ddx chases bad ptr */
        if (pScreen->pScratchPixmap)
            (*pScreen->DestroyPixmap) (pPixmap);
        else
            pScreen->pScratchPixmap = pPixmap;
    }
}

Bool
CreateScratchPixmapsForScreen(ScreenPtr pScreen)
{
    unsigned int pixmap_size;

    pixmap_size = sizeof(PixmapRec) + dixScreenSpecificPrivatesSize(pScreen, PRIVATE_PIXMAP);
    pScreen->totalPixmapSize =
        BitmapBytePad(pixmap_size * 8);

    /* let it be created on first use */
    pScreen->pScratchPixmap = NULL;
    return TRUE;
}

void
FreeScratchPixmapsForScreen(ScreenPtr pScreen)
{
    FreeScratchPixmapHeader(pScreen->pScratchPixmap);
}

/* callable by ddx */
PixmapPtr
AllocatePixmap(ScreenPtr pScreen, int pixDataSize)
{
    PixmapPtr pPixmap;

    assert(pScreen->totalPixmapSize > 0);

    if (pScreen->totalPixmapSize > ((size_t) - 1) - pixDataSize)
        return NullPixmap;

    pPixmap = malloc(pScreen->totalPixmapSize + pixDataSize);
    if (!pPixmap)
        return NullPixmap;

    dixInitScreenPrivates(pScreen, pPixmap, pPixmap + 1, PRIVATE_PIXMAP);
    return pPixmap;
}

/* callable by ddx */
void
FreePixmap(PixmapPtr pPixmap)
{
    dixFiniPrivates(pPixmap, PRIVATE_PIXMAP);
    free(pPixmap);
}

PixmapPtr PixmapShareToSlave(PixmapPtr pixmap, ScreenPtr slave)
{
    PixmapPtr spix;
    int ret;
    void *handle;
    ScreenPtr master = pixmap->drawable.pScreen;
    int depth = pixmap->drawable.depth;

    ret = master->SharePixmapBacking(pixmap, slave, &handle);
    if (ret == FALSE)
        return NULL;

    spix = slave->CreatePixmap(slave, 0, 0, depth,
                               CREATE_PIXMAP_USAGE_SHARED);
    slave->ModifyPixmapHeader(spix, pixmap->drawable.width,
                              pixmap->drawable.height, depth, 0,
                              pixmap->devKind, NULL);

    /* have the slave pixmap take a reference on the master pixmap
       later we destroy them both at the same time */
    pixmap->refcnt++;

    spix->master_pixmap = pixmap;

    ret = slave->SetSharedPixmapBacking(spix, handle);
    if (ret == FALSE) {
        slave->DestroyPixmap(spix);
        return NULL;
    }

    return spix;
}

Bool
PixmapStartDirtyTracking2(PixmapPtr src,
			  PixmapPtr slave_dst,
			  int x, int y, int dst_x, int dst_y)
{
    ScreenPtr screen = src->drawable.pScreen;
    PixmapDirtyUpdatePtr dirty_update;

    dirty_update = calloc(1, sizeof(PixmapDirtyUpdateRec));
    if (!dirty_update)
        return FALSE;

    dirty_update->src = src;
    dirty_update->slave_dst = slave_dst;
    dirty_update->x = x;
    dirty_update->y = y;
    dirty_update->dst_x = dst_x;
    dirty_update->dst_y = dst_y;

    dirty_update->damage = DamageCreate(NULL, NULL,
                                        DamageReportNone,
                                        TRUE, src->drawable.pScreen,
                                        src->drawable.pScreen);
    if (!dirty_update->damage) {
        free(dirty_update);
        return FALSE;
    }

    DamageRegister(&src->drawable, dirty_update->damage);
    xorg_list_add(&dirty_update->ent, &screen->pixmap_dirty_list);
    return TRUE;
}

Bool
PixmapStartDirtyTracking(PixmapPtr src,
			 PixmapPtr slave_dst,
			 int x, int y)
{
   return PixmapStartDirtyTracking2(src, slave_dst, x, y, 0, 0);
}

Bool
PixmapStopDirtyTracking(PixmapPtr src, PixmapPtr slave_dst)
{
    ScreenPtr screen = src->drawable.pScreen;
    PixmapDirtyUpdatePtr ent, safe;

    xorg_list_for_each_entry_safe(ent, safe, &screen->pixmap_dirty_list, ent) {
        if (ent->src == src && ent->slave_dst == slave_dst) {
            DamageDestroy(ent->damage);
            xorg_list_del(&ent->ent);
            free(ent);
        }
    }
    return TRUE;
}

/*
 * this function can possibly be improved and optimised, by clipping
 * instead of iterating
 */
Bool PixmapSyncDirtyHelper(PixmapDirtyUpdatePtr dirty, RegionPtr dirty_region)
{
    ScreenPtr pScreen = dirty->src->drawable.pScreen;
    int n;
    BoxPtr b;
    RegionPtr region = DamageRegion(dirty->damage);
    GCPtr pGC;
    PixmapPtr dst;
    SourceValidateProcPtr SourceValidate;

    /*
     * SourceValidate is used by the software cursor code
     * to pull the cursor off of the screen when reading
     * bits from the frame buffer. Bypassing this function
     * leaves the software cursor in place
     */
    SourceValidate = pScreen->SourceValidate;
    pScreen->SourceValidate = NULL;

    RegionTranslate(dirty_region, dirty->x, dirty->y);
    RegionIntersect(dirty_region, dirty_region, region);

    if (RegionNil(dirty_region)) {
        RegionUninit(dirty_region);
        return FALSE;
    }

    dst = dirty->slave_dst->master_pixmap;
    if (!dst)
        dst = dirty->slave_dst;

    RegionTranslate(dirty_region, -dirty->x, -dirty->y);
    n = RegionNumRects(dirty_region);
    b = RegionRects(dirty_region);

    pGC = GetScratchGC(dirty->src->drawable.depth, pScreen);
    ValidateGC(&dst->drawable, pGC);

    while (n--) {
        BoxRec dst_box;
        int w, h;

        dst_box = *b;
        w = dst_box.x2 - dst_box.x1;
        h = dst_box.y2 - dst_box.y1;

        pGC->ops->CopyArea(&dirty->src->drawable, &dst->drawable, pGC,
                           dirty->x + dst_box.x1, dirty->y + dst_box.y1, w, h, dirty->dst_x + dst_box.x1, dirty->dst_y + dst_box.y1);
        b++;
    }
    FreeScratchGC(pGC);

    pScreen->SourceValidate = SourceValidate;
    return TRUE;
}
