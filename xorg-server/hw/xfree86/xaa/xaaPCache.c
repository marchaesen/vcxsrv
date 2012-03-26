
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "gc.h"
#include "mi.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "regionstr.h"
#include "servermd.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaacexp.h"
#include "xaalocal.h"
#include "xaawrap.h"

#define MAX_COLOR	32
#define MAX_MONO	32
#define MAX_8		32
#define MAX_128		32
#define MAX_256		32
#define MAX_512		16

static int CacheInitIndex = -1;

#define CACHEINIT(p) ((p)->privates[CacheInitIndex].val)

typedef struct _CacheLink {
    int x;
    int y;
    int w;
    int h;
    struct _CacheLink *next;
} CacheLink, *CacheLinkPtr;

static void
TransferList(CacheLinkPtr list, XAACacheInfoPtr array, int num)
{
    while (num--) {
        array->x = list->x;
        array->y = list->y;
        array->w = list->w;
        array->h = list->h;
        array->serialNumber = 0;
        array->fg = array->bg = -1;
        list = list->next;
        array++;
    }
}

static CacheLinkPtr
Enlist(CacheLinkPtr link, int x, int y, int w, int h)
{
    CacheLinkPtr newLink;

    newLink = malloc(sizeof(CacheLink));
    newLink->next = link;
    newLink->x = x;
    newLink->y = y;
    newLink->w = w;
    newLink->h = h;
    return newLink;
}

static CacheLinkPtr
Delist(CacheLinkPtr link)
{
    CacheLinkPtr ret = NULL;

    if (link) {
        ret = link->next;
        free(link);
    }
    return ret;
}

static void
FreeList(CacheLinkPtr link)
{
    CacheLinkPtr tmp;

    while (link) {
        tmp = link;
        link = link->next;
        free(tmp);
    }
}

static CacheLinkPtr
QuadLinks(CacheLinkPtr big, CacheLinkPtr little)
{
    /* CAUTION: This doesn't free big */
    int w1, w2, h1, h2;

    while (big) {
        w1 = big->w >> 1;
        w2 = big->w - w1;
        h1 = big->h >> 1;
        h2 = big->h - h1;

        little = Enlist(little, big->x, big->y, w1, h1);
        little = Enlist(little, big->x + w1, big->y, w2, h1);
        little = Enlist(little, big->x, big->y + h1, w1, h2);
        little = Enlist(little, big->x + w1, big->y + h1, w2, h2);

        big = big->next;
    }
    return little;
}

static void
SubdivideList(CacheLinkPtr * large, CacheLinkPtr * small)
{
    CacheLinkPtr big = *large;
    CacheLinkPtr little = *small;
    int size = big->w >> 1;

    little = Enlist(little, big->x, big->y, size, size);
    little = Enlist(little, big->x + size, big->y, size, size);
    little = Enlist(little, big->x, big->y + size, size, size);
    little = Enlist(little, big->x + size, big->y + size, size, size);
    *small = little;
    big = Delist(big);
    *large = big;
}

static void
FreePixmapCachePrivate(XAAPixmapCachePrivatePtr pPriv)
{
    if (!pPriv)
        return;

    free(pPriv->Info512);
    free(pPriv->Info256);
    free(pPriv->Info128);
    free(pPriv->InfoColor);
    free(pPriv->InfoMono);
    free(pPriv->InfoPartial);

    free(pPriv);
}

void
XAAClosePixmapCache(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);

    if (infoRec->PixmapCachePrivate)
        FreePixmapCachePrivate((XAAPixmapCachePrivatePtr) infoRec->
                               PixmapCachePrivate);

    infoRec->PixmapCachePrivate = NULL;
}

static CacheLinkPtr
ThinOutPartials(CacheLinkPtr ListPartial, int *num, int *maxw, int *maxh)
{
/* This guy's job is to get at least 4 big slots out of a list of fragments */

    CacheLinkPtr List64, List32, List16, List8, pCur, next, ListKeepers;
    int Num64, Num32, Num16, Num8, NumKeepers;
    int w, h;

    List64 = List32 = List16 = List8 = ListKeepers = NULL;
    Num64 = Num32 = Num16 = Num8 = NumKeepers = 0;
    w = h = 0;

    /* We sort partials by how large a square tile they can cache.
       If a partial can't store a 64x64, 32x32, 16x16 or 8x8 tile,
       we free it.  */

    pCur = ListPartial;
    while (pCur) {
        next = pCur->next;
        if ((pCur->w >= 64) && (pCur->h >= 64)) {
            pCur->next = List64;
            List64 = pCur;
            Num64++;
        }
        else if ((pCur->w >= 32) && (pCur->h >= 32)) {
            pCur->next = List32;
            List32 = pCur;
            Num32++;
        }
        else if ((pCur->w >= 16) && (pCur->h >= 16)) {
            pCur->next = List16;
            List16 = pCur;
            Num16++;
        }
        else if ((pCur->w >= 8) && (pCur->h >= 8)) {
            pCur->next = List8;
            List8 = pCur;
            Num8++;
        }
        else {
            free(pCur);
        }

        pCur = next;
    }

    /* We save all the tiles from the largest bin that we can get
       at least 4 of.  If there are too few of a bigger slot, we
       cut it in fourths to make smaller slots. */

    if (Num64 >= 4) {
        ListKeepers = List64;
        List64 = NULL;
        NumKeepers = Num64;
        goto GOT_EM;
    }
    else if (Num64) {
        List32 = QuadLinks(List64, List32);
        Num32 += Num64 * 4;
        Num64 = 0;
    }

    if (Num32 >= 4) {
        ListKeepers = List32;
        List32 = NULL;
        NumKeepers = Num32;
        goto GOT_EM;
    }
    else if (Num32) {
        List16 = QuadLinks(List32, List16);
        Num16 += Num32 * 4;
        Num32 = 0;
    }

    if (Num16 >= 4) {
        ListKeepers = List16;
        List16 = NULL;
        NumKeepers = Num16;
        goto GOT_EM;
    }
    else if (Num16) {
        List8 = QuadLinks(List16, List8);
        Num8 += Num16 * 4;
        Num16 = 0;
    }

    if (Num8 >= 4) {
        ListKeepers = List8;
        List8 = NULL;
        NumKeepers = Num8;
        goto GOT_EM;
    }

 GOT_EM:

    /* Free the ones we aren't using */

    if (List64)
        FreeList(List64);
    if (List32)
        FreeList(List32);
    if (List16)
        FreeList(List16);
    if (List8)
        FreeList(List8);

    /* Enlarge the slots if we can */

    if (ListKeepers) {
        CacheLinkPtr pLink = ListKeepers;

        w = h = 128;

        while (pLink) {
            if (pLink->w < w)
                w = pLink->w;
            if (pLink->h < h)
                h = pLink->h;
            pLink = pLink->next;
        }
    }

    *maxw = w;
    *maxh = h;
    *num = NumKeepers;
    return ListKeepers;
}

static void
ConvertColorToMono(CacheLinkPtr * ColorList,
                   int ColorW, int ColorH,
                   CacheLinkPtr * MonoList, int MonoW, int MonoH)
{
    int x, y, w;

    x = (*ColorList)->x;
    y = (*ColorList)->y;
    *ColorList = Delist(*ColorList);

    while (ColorH) {
        ColorH -= MonoH;
        for (w = 0; w <= (ColorW - MonoW); w += MonoW)
            *MonoList = Enlist(*MonoList, x + w, y + ColorH, MonoW, MonoH);
    }
}

static void
ConvertAllPartialsTo8x8(int *NumMono, int *NumColor,
                        CacheLinkPtr ListPartial,
                        CacheLinkPtr * ListMono,
                        CacheLinkPtr * ListColor, XAAInfoRecPtr infoRec)
{
/* This guy extracts as many 8x8 slots as it can out of fragments */

    int ColorH = infoRec->CacheHeightColor8x8Pattern;
    int ColorW = infoRec->CacheWidthColor8x8Pattern;
    int MonoH = infoRec->CacheHeightMono8x8Pattern;
    int MonoW = infoRec->CacheWidthMono8x8Pattern;
    int x, y, w, Height, Width;
    Bool DoColor = (infoRec->PixmapCacheFlags & CACHE_COLOR_8x8);
    Bool DoMono = (infoRec->PixmapCacheFlags & CACHE_MONO_8x8);
    CacheLinkPtr pLink = ListPartial;
    CacheLinkPtr MonoList = *ListMono, ColorList = *ListColor;

    if (DoColor && DoMono) {
        /* we assume color patterns take more space than color ones */
        if (MonoH > ColorH)
            ColorH = MonoH;
        if (MonoW > ColorW)
            ColorW = MonoW;
    }

    /* Break up the area into as many Color and Mono slots as we can */

    while (pLink) {
        Height = pLink->h;
        Width = pLink->w;
        x = pLink->x;
        y = pLink->y;

        if (DoColor) {
            while (Height >= ColorH) {
                Height -= ColorH;
                for (w = 0; w <= (Width - ColorW); w += ColorW) {
                    ColorList =
                        Enlist(ColorList, x + w, y + Height, ColorW, ColorH);
                    (*NumColor)++;
                }
            }
        }

        if (DoMono && (Height >= MonoH)) {
            while (Height >= MonoH) {
                Height -= MonoH;
                for (w = 0; w <= (Width - MonoW); w += MonoW) {
                    MonoList =
                        Enlist(MonoList, x + w, y + Height, MonoW, MonoH);
                    (*NumMono)++;
                }
            }
        }

        pLink = pLink->next;
    }

    *ListMono = MonoList;
    *ListColor = ColorList;
    FreeList(ListPartial);
}

static CacheLinkPtr
ExtractOneThatFits(CacheLinkPtr * initList, int w, int h)
{
    CacheLinkPtr list = *initList;
    CacheLinkPtr prev = NULL;

    while (list) {
        if ((list->w >= w) && (list->h >= h))
            break;
        prev = list;
        list = list->next;
    }

    if (list) {
        if (prev)
            prev->next = list->next;
        else
            *initList = list->next;

        list->next = NULL;
    }

    return list;
}

static CacheLinkPtr
ConvertSomePartialsTo8x8(int *NumMono, int *NumColor, int *NumPartial,
                         CacheLinkPtr ListPartial,
                         CacheLinkPtr * ListMono,
                         CacheLinkPtr * ListColor,
                         int *maxw, int *maxh, XAAInfoRecPtr infoRec)
{
/* This guy tries to get 4 of each type of 8x8 slot requested out of
   a list of fragments all while trying to retain some big fragments
   for the cache blits */

    int ColorH = infoRec->CacheHeightColor8x8Pattern;
    int ColorW = infoRec->CacheWidthColor8x8Pattern;
    int MonoH = infoRec->CacheHeightMono8x8Pattern;
    int MonoW = infoRec->CacheWidthMono8x8Pattern;
    Bool DoColor = (infoRec->PixmapCacheFlags & CACHE_COLOR_8x8);
    Bool DoMono = (infoRec->PixmapCacheFlags & CACHE_MONO_8x8);
    CacheLinkPtr List64, List32, List16, List8, pCur, next, ListKeepers;
    CacheLinkPtr MonoList = *ListMono, ColorList = *ListColor;
    int Num64, Num32, Num16, Num8, NumKeepers;
    int w, h, Width, Height;
    int MonosPerColor = 1;

    if (DoColor && DoMono) {
        /* we assume color patterns take more space than color ones */
        if (MonoH > ColorH)
            ColorH = MonoH;
        if (MonoW > ColorW)
            ColorW = MonoW;
        MonosPerColor = (ColorH / MonoH) * (ColorW / MonoW);
    }

    List64 = List32 = List16 = List8 = ListKeepers = MonoList = ColorList =
        NULL;
    Num64 = Num32 = Num16 = Num8 = NumKeepers = 0;
    Width = Height = 0;

    /* We sort partials by how large a square tile they can cache.
       We make 8x8 patterns from the leftovers if we can. */

    pCur = ListPartial;
    while (pCur) {
        next = pCur->next;
        if ((pCur->w >= 64) && (pCur->h >= 64)) {
            pCur->next = List64;
            List64 = pCur;
            Num64++;
        }
        else if ((pCur->w >= 32) && (pCur->h >= 32)) {
            pCur->next = List32;
            List32 = pCur;
            Num32++;
        }
        else if ((pCur->w >= 16) && (pCur->h >= 16)) {
            pCur->next = List16;
            List16 = pCur;
            Num16++;
        }
        else if ((pCur->w >= 8) && (pCur->h >= 8)) {
            pCur->next = List8;
            List8 = pCur;
            Num8++;
        }
        else {
            h = pCur->h;
            if (DoColor && (pCur->w >= ColorW) && (h >= ColorH)) {
                while (h >= ColorH) {
                    h -= ColorH;
                    for (w = 0; w <= (pCur->w - ColorW); w += ColorW) {
                        ColorList = Enlist(ColorList,
                                           pCur->x + w, pCur->y + h, ColorW,
                                           ColorH);
                        (*NumColor)++;
                    }
                }
            }
            if (DoMono && (pCur->w >= MonoW) && (h >= MonoH)) {
                while (h >= MonoH) {
                    h -= MonoH;
                    for (w = 0; w <= (pCur->w - MonoW); w += MonoW) {
                        MonoList = Enlist(MonoList,
                                          pCur->x + w, pCur->y + h, MonoW,
                                          MonoH);
                        (*NumMono)++;
                    }
                }
            }
            free(pCur);
        }

        pCur = next;
    }

    /* Try to extract at least 4 of each type of 8x8 slot that we need */

    if (DoColor) {
        CacheLinkPtr theOne;

        while (*NumColor < 4) {
            theOne = NULL;
            if (Num8) {
                if ((theOne = ExtractOneThatFits(&List8, ColorW, ColorH)))
                    Num8--;
            }
            if (Num16 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List16, ColorW, ColorH)))
                    Num16--;
            }
            if (Num32 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List32, ColorW, ColorH)))
                    Num32--;
            }
            if (Num64 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List64, ColorW, ColorH)))
                    Num64--;
            }

            if (!theOne)
                break;

            ConvertAllPartialsTo8x8(NumMono, NumColor, theOne,
                                    &MonoList, &ColorList, infoRec);

            if (DoMono) {
                while (*NumColor && (*NumMono < 4)) {
                    ConvertColorToMono(&ColorList, ColorW, ColorH,
                                       &MonoList, MonoW, MonoH);
                    (*NumColor)--;
                    *NumMono += MonosPerColor;
                }
            }
        }
    }

    if (DoMono) {
        CacheLinkPtr theOne;

        while (*NumMono < 4) {
            theOne = NULL;
            if (Num8) {
                if ((theOne = ExtractOneThatFits(&List8, MonoW, MonoH)))
                    Num8--;
            }
            if (Num16 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List16, MonoW, MonoH)))
                    Num16--;
            }
            if (Num32 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List32, MonoW, MonoH)))
                    Num32--;
            }
            if (Num64 && !theOne) {
                if ((theOne = ExtractOneThatFits(&List64, MonoW, MonoH)))
                    Num64--;
            }

            if (!theOne)
                break;

            ConvertAllPartialsTo8x8(NumMono, NumColor, theOne,
                                    &MonoList, &ColorList, infoRec);
        }
    }

    /* We save all the tiles from the largest bin that we can get
       at least 4 of.  If there are too few of a bigger slot, we
       cut it in fourths to make smaller slots. */

    if (Num64 >= 4) {
        ListKeepers = List64;
        List64 = NULL;
        NumKeepers = Num64;
        goto GOT_EM;
    }
    else if (Num64) {
        List32 = QuadLinks(List64, List32);
        Num32 += Num64 * 4;
        Num64 = 0;
    }

    if (Num32 >= 4) {
        ListKeepers = List32;
        List32 = NULL;
        NumKeepers = Num32;
        goto GOT_EM;
    }
    else if (Num32) {
        List16 = QuadLinks(List32, List16);
        Num16 += Num32 * 4;
        Num32 = 0;
    }

    if (Num16 >= 4) {
        ListKeepers = List16;
        List16 = NULL;
        NumKeepers = Num16;
        goto GOT_EM;
    }
    else if (Num16) {
        List8 = QuadLinks(List16, List8);
        Num8 += Num16 * 4;
        Num16 = 0;
    }

    if (Num8 >= 4) {
        ListKeepers = List8;
        List8 = NULL;
        NumKeepers = Num8;
        goto GOT_EM;
    }

 GOT_EM:

    /* Free the ones we aren't using */

    if (List64)
        ConvertAllPartialsTo8x8(NumMono, NumColor, List64,
                                &MonoList, &ColorList, infoRec);
    if (List32)
        ConvertAllPartialsTo8x8(NumMono, NumColor, List32,
                                &MonoList, &ColorList, infoRec);
    if (List16)
        ConvertAllPartialsTo8x8(NumMono, NumColor, List16,
                                &MonoList, &ColorList, infoRec);
    if (List8)
        ConvertAllPartialsTo8x8(NumMono, NumColor, List8,
                                &MonoList, &ColorList, infoRec);

    /* Enlarge the slots if we can */

    if (ListKeepers) {
        CacheLinkPtr pLink = ListKeepers;

        Width = Height = 128;

        while (pLink) {
            if (pLink->w < Width)
                Width = pLink->w;
            if (pLink->h < Height)
                Height = pLink->h;
            pLink = pLink->next;
        }
    }

    *ListMono = MonoList;
    *ListColor = ColorList;
    *maxw = Width;
    *maxh = Height;
    *NumPartial = NumKeepers;
    return ListKeepers;
}

void
XAAInitPixmapCache(ScreenPtr pScreen, RegionPtr areas, pointer data)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAInfoRecPtr infoRec = (XAAInfoRecPtr) data;
    XAAPixmapCachePrivatePtr pCachePriv;
    BoxPtr pBox = RegionRects(areas);
    int nBox = RegionNumRects(areas);
    int Num512, Num256, Num128, NumPartial, NumColor, NumMono;
    int Target512, Target256;
    CacheLinkPtr List512, List256, List128, ListPartial, ListColor, ListMono;
    int x, y, w, h, ntotal, granularity, width, height, i;
    int MaxPartialWidth, MaxPartialHeight;

    infoRec->MaxCacheableTileWidth = 0;
    infoRec->MaxCacheableTileHeight = 0;
    infoRec->MaxCacheableStippleHeight = 0;
    infoRec->MaxCacheableStippleWidth = 0;
    infoRec->UsingPixmapCache = FALSE;

    if (!nBox || !pBox || !(infoRec->Flags & PIXMAP_CACHE))
        return;

    /* Allocate a persistent per-screen init flag to control messages */
    if (CacheInitIndex < 0)
        CacheInitIndex = xf86AllocateScrnInfoPrivateIndex();

    /* free the old private data if it exists */
    if (infoRec->PixmapCachePrivate) {
        FreePixmapCachePrivate((XAAPixmapCachePrivatePtr) infoRec->
                               PixmapCachePrivate);
        infoRec->PixmapCachePrivate = NULL;
    }

    Num512 = Num256 = Num128 = NumPartial = NumMono = NumColor = 0;
    List512 = List256 = List128 = ListPartial = ListMono = ListColor = NULL;
    granularity = infoRec->CachePixelGranularity;
    if (granularity <= 1)
        granularity = 0;

    /* go through the boxes and break it into as many pieces as we can fit */

    while (nBox--) {
        x = pBox->x1;
        if (granularity) {
            int tmp = x % granularity;

            if (tmp)
                x += (granularity - tmp);
        }
        width = pBox->x2 - x;
        if (width <= 0) {
            pBox++;
            continue;
        }

        y = pBox->y1;
        height = pBox->y2 - y;

        for (h = 0; h <= (height - 512); h += 512) {
            for (w = 0; w <= (width - 512); w += 512) {
                List512 = Enlist(List512, x + w, y + h, 512, 512);
                Num512++;
            }
            for (; w <= (width - 256); w += 256) {
                List256 = Enlist(List256, x + w, y + h, 256, 256);
                List256 = Enlist(List256, x + w, y + h + 256, 256, 256);
                Num256 += 2;
            }
            for (; w <= (width - 128); w += 128) {
                List128 = Enlist(List128, x + w, y + h, 128, 128);
                List128 = Enlist(List128, x + w, y + h + 128, 128, 128);
                List128 = Enlist(List128, x + w, y + h + 256, 128, 128);
                List128 = Enlist(List128, x + w, y + h + 384, 128, 128);
                Num128 += 4;
            }
            if (w < width) {
                int d = width - w;

                ListPartial = Enlist(ListPartial, x + w, y + h, d, 128);
                ListPartial = Enlist(ListPartial, x + w, y + h + 128, d, 128);
                ListPartial = Enlist(ListPartial, x + w, y + h + 256, d, 128);
                ListPartial = Enlist(ListPartial, x + w, y + h + 384, d, 128);
                NumPartial += 4;
            }
        }
        for (; h <= (height - 256); h += 256) {
            for (w = 0; w <= (width - 256); w += 256) {
                List256 = Enlist(List256, x + w, y + h, 256, 256);
                Num256++;
            }
            for (; w <= (width - 128); w += 128) {
                List128 = Enlist(List128, x + w, y + h, 128, 128);
                List128 = Enlist(List128, x + w, y + h + 128, 128, 128);
                Num128 += 2;
            }
            if (w < width) {
                int d = width - w;

                ListPartial = Enlist(ListPartial, x + w, y + h, d, 128);
                ListPartial = Enlist(ListPartial, x + w, y + h + 128, d, 128);
                NumPartial += 2;
            }
        }
        for (; h <= (height - 128); h += 128) {
            for (w = 0; w <= (width - 128); w += 128) {
                List128 = Enlist(List128, x + w, y + h, 128, 128);
                Num128++;
            }
            if (w < width) {
                ListPartial = Enlist(ListPartial, x + w, y + h, width - w, 128);
                NumPartial++;
            }
        }
        if (h < height) {
            int d = height - h;

            for (w = 0; w <= (width - 128); w += 128) {
                ListPartial = Enlist(ListPartial, x + w, y + h, 128, d);
                NumPartial++;
            }
            if (w < width) {
                ListPartial = Enlist(ListPartial, x + w, y + h, width - w, d);
                NumPartial++;
            }
        }
        pBox++;
    }

/* 
   by this point we've carved the space into as many 512x512, 256x256
	and 128x128 blocks as we could fit.  We will then break larger
	blocks into smaller ones if we need to.  The rules are as follows:

     512x512 -
	1) Don't take up more than half the memory.
	2) Don't bother if you can't get at least four.
	3) Don't make more than MAX_512.
	4) Don't have any of there are no 256x256s.

     256x256 -
	1) Don't take up more than a quarter of the memory enless there
		aren't any 512x512s.  Then we can take up to half.
	2) Don't bother if you can't get at least four.
	3) Don't make more than MAX_256.

     128x128 -
	1) Don't make more than MAX_128.

     We don't bother with the partial blocks unless we can use them
     for 8x8 pattern fills or we are short on larger blocks.

*/

    ntotal = Num128 + (Num256 << 2) + (Num512 << 4);

    Target512 = ntotal >> 5;
    if (Target512 < 4)
        Target512 = 0;
    if (!Target512)
        Target256 = ntotal >> 3;
    else
        Target256 = ntotal >> 4;
    if (Target256 < 4)
        Target256 = 0;

    if (Num512 && Num256 < 4) {
        while (Num512 && Num256 < Target256) {
            SubdivideList(&List512, &List256);
            Num256 += 4;
            Num512--;
        }
    }

    if (!Num512) {              /* no room */
    }
    else if ((Num512 < 4) || (!Target512)) {
        while (Num512) {
            SubdivideList(&List512, &List256);
            Num256 += 4;
            Num512--;
        }
    }
    else if ((Num512 > MAX_512) || (Num512 > Target512)) {
        while (Num512 > MAX_512) {
            SubdivideList(&List512, &List256);
            Num256 += 4;
            Num512--;
        }
        while (Num512 > Target512) {
            if (Num256 < MAX_256) {
                SubdivideList(&List512, &List256);
                Num256 += 4;
                Num512--;
            }
            else
                break;
        }
    }

    if (!Num256) {              /* no room */
    }
    else if ((Num256 < 4) || (!Target256)) {
        while (Num256) {
            SubdivideList(&List256, &List128);
            Num128 += 4;
            Num256--;
        }
    }
    else if ((Num256 > MAX_256) || (Num256 > Target256)) {
        while (Num256 > MAX_256) {
            SubdivideList(&List256, &List128);
            Num128 += 4;
            Num256--;
        }
        while (Num256 > Target256) {
            if (Num128 < MAX_128) {
                SubdivideList(&List256, &List128);
                Num128 += 4;
                Num256--;
            }
            else
                break;
        }
    }

    if (Num128 && ((Num128 < 4) || (Num128 > MAX_128))) {
        CacheLinkPtr next;
        int max = (Num128 > MAX_128) ? MAX_128 : 0;

        /*
         * Note: next is set in this way to work around a code generation
         * bug in gcc 2.7.2.3.
         */
        next = List128->next;
        while (Num128 > max) {
            List128->next = ListPartial;
            ListPartial = List128;
            if ((List128 = next))
                next = List128->next;
            NumPartial++;
            Num128--;
        }
    }

    MaxPartialHeight = MaxPartialWidth = 0;

    /* at this point we have as many 512x512 and 256x256 slots as we
       want but may have an excess of 128x128 slots.  We still need
       to find out if we need 8x8 slots.  We take these from the
       partials if we have them.  Otherwise, we break some 128x128's */

    if (!(infoRec->PixmapCacheFlags & (CACHE_MONO_8x8 | CACHE_COLOR_8x8))) {
        if (NumPartial) {
            if (Num128) {       /* don't bother with partials */
                FreeList(ListPartial);
                NumPartial = 0;
                ListPartial = NULL;
            }
            else {
                /* We have no big slots.  Weed out the unusable partials */
                ListPartial = ThinOutPartials(ListPartial, &NumPartial,
                                              &MaxPartialWidth,
                                              &MaxPartialHeight);
            }
        }
    }
    else {
        int MonosPerColor = 1;
        int ColorH = infoRec->CacheHeightColor8x8Pattern;
        int ColorW = infoRec->CacheWidthColor8x8Pattern;
        int MonoH = infoRec->CacheHeightMono8x8Pattern;
        int MonoW = infoRec->CacheWidthMono8x8Pattern;
        Bool DoColor = (infoRec->PixmapCacheFlags & CACHE_COLOR_8x8);
        Bool DoMono = (infoRec->PixmapCacheFlags & CACHE_MONO_8x8);

        if (DoColor)
            infoRec->CanDoColor8x8 = FALSE;
        if (DoMono)
            infoRec->CanDoMono8x8 = FALSE;

        if (DoColor && DoMono) {
            /* we assume color patterns take more space than color ones */
            if (MonoH > ColorH)
                ColorH = MonoH;
            if (MonoW > ColorW)
                ColorW = MonoW;
            MonosPerColor = (ColorH / MonoH) * (ColorW / MonoW);
        }

        if (Num128) {
            if (NumPartial) {   /* use all for 8x8 slots */
                ConvertAllPartialsTo8x8(&NumMono, &NumColor,
                                        ListPartial, &ListMono, &ListColor,
                                        infoRec);
                NumPartial = 0;
                ListPartial = NULL;
            }

            /* Get some 8x8 slots from the 128 slots */
            while ((Num128 > 4) &&
                   ((NumMono < MAX_MONO) && (NumColor < MAX_COLOR))) {
                CacheLinkPtr tmp = NULL;

                tmp = Enlist(tmp, List128->x, List128->y,
                             List128->w, List128->h);
                List128 = Delist(List128);
                Num128--;

                ConvertAllPartialsTo8x8(&NumMono, &NumColor,
                                        tmp, &ListMono, &ListColor, infoRec);
            }
        }
        else if (NumPartial) {
            /* We have share partials between 8x8 slots and tiles. */
            ListPartial = ConvertSomePartialsTo8x8(&NumMono, &NumColor,
                                                   &NumPartial, ListPartial,
                                                   &ListMono, &ListColor,
                                                   &MaxPartialWidth,
                                                   &MaxPartialHeight, infoRec);
        }

        if (DoMono && DoColor) {
            if (NumColor && ((NumColor > MAX_COLOR) || (NumColor < 4))) {
                int max = (NumColor > MAX_COLOR) ? MAX_COLOR : 0;

                while (NumColor > max) {
                    ConvertColorToMono(&ListColor, ColorW, ColorH,
                                       &ListMono, MonoW, MonoH);
                    NumColor--;
                    NumMono += MonosPerColor;
                }
            }

            /* favor Mono slots over Color ones */
            while ((NumColor > 4) && (NumMono < MAX_MONO)) {
                ConvertColorToMono(&ListColor, ColorW, ColorH,
                                   &ListMono, MonoW, MonoH);
                NumColor--;
                NumMono += MonosPerColor;
            }
        }

        if (NumMono && ((NumMono > MAX_MONO) || (NumMono < 4))) {
            int max = (NumMono > MAX_MONO) ? MAX_MONO : 0;

            while (NumMono > max) {
                ListMono = Delist(ListMono);
                NumMono--;
            }
        }
        if (NumColor && ((NumColor > MAX_COLOR) || (NumColor < 4))) {
            int max = (NumColor > MAX_COLOR) ? MAX_COLOR : 0;

            while (NumColor > max) {
                ListColor = Delist(ListColor);
                NumColor--;
            }
        }
    }

    pCachePriv = calloc(1, sizeof(XAAPixmapCachePrivate));
    if (!pCachePriv) {
        if (Num512)
            FreeList(List512);
        if (Num256)
            FreeList(List256);
        if (Num128)
            FreeList(List128);
        if (NumPartial)
            FreeList(ListPartial);
        if (NumColor)
            FreeList(ListColor);
        if (NumMono)
            FreeList(ListMono);
        return;
    }

    infoRec->PixmapCachePrivate = (char *) pCachePriv;

    if (Num512) {
        pCachePriv->Info512 = calloc(Num512, sizeof(XAACacheInfoRec));
        if (!pCachePriv->Info512)
            Num512 = 0;
        if (Num512)
            TransferList(List512, pCachePriv->Info512, Num512);
        FreeList(List512);
        pCachePriv->Num512x512 = Num512;
    }
    if (Num256) {
        pCachePriv->Info256 = calloc(Num256, sizeof(XAACacheInfoRec));
        if (!pCachePriv->Info256)
            Num256 = 0;
        if (Num256)
            TransferList(List256, pCachePriv->Info256, Num256);
        FreeList(List256);
        pCachePriv->Num256x256 = Num256;
    }
    if (Num128) {
        pCachePriv->Info128 = calloc(Num128, sizeof(XAACacheInfoRec));
        if (!pCachePriv->Info128)
            Num128 = 0;
        if (Num128)
            TransferList(List128, pCachePriv->Info128, Num128);
        FreeList(List128);
        pCachePriv->Num128x128 = Num128;
    }

    if (NumPartial) {
        pCachePriv->InfoPartial = calloc(NumPartial, sizeof(XAACacheInfoRec));
        if (!pCachePriv->InfoPartial)
            NumPartial = 0;
        if (NumPartial)
            TransferList(ListPartial, pCachePriv->InfoPartial, NumPartial);
        FreeList(ListPartial);
        pCachePriv->NumPartial = NumPartial;
    }

    if (NumColor) {
        pCachePriv->InfoColor = calloc(NumColor, sizeof(XAACacheInfoRec));
        if (!pCachePriv->InfoColor)
            NumColor = 0;
        if (NumColor)
            TransferList(ListColor, pCachePriv->InfoColor, NumColor);
        FreeList(ListColor);
        pCachePriv->NumColor = NumColor;
    }

    if (NumMono) {
        pCachePriv->InfoMono = calloc(NumMono, sizeof(XAACacheInfoRec));
        if (!pCachePriv->InfoMono)
            NumMono = 0;
        if (NumMono)
            TransferList(ListMono, pCachePriv->InfoMono, NumMono);
        FreeList(ListMono);
        pCachePriv->NumMono = NumMono;
    }

    if (NumPartial) {
        infoRec->MaxCacheableTileWidth = MaxPartialWidth;
        infoRec->MaxCacheableTileHeight = MaxPartialHeight;
    }
    if (Num128)
        infoRec->MaxCacheableTileWidth = infoRec->MaxCacheableTileHeight = 128;
    if (Num256)
        infoRec->MaxCacheableTileWidth = infoRec->MaxCacheableTileHeight = 256;
    if (Num512)
        infoRec->MaxCacheableTileWidth = infoRec->MaxCacheableTileHeight = 512;

    infoRec->MaxCacheableStippleHeight = infoRec->MaxCacheableTileHeight;
    infoRec->MaxCacheableStippleWidth =
        infoRec->MaxCacheableTileWidth * pScrn->bitsPerPixel;
    if (infoRec->ScreenToScreenColorExpandFillFlags & TRIPLE_BITS_24BPP)
        infoRec->MaxCacheableStippleWidth /= 3;

    if (NumMono) {
        if (!(infoRec->Mono8x8PatternFillFlags &
              (HARDWARE_PATTERN_PROGRAMMED_ORIGIN |
               HARDWARE_PATTERN_PROGRAMMED_BITS))) {
            int numPerLine =
                infoRec->CacheWidthMono8x8Pattern / infoRec->MonoPatternPitch;

            for (i = 0; i < 64; i++) {
                pCachePriv->MonoOffsets[i].y = i / numPerLine;
                pCachePriv->MonoOffsets[i].x = (i % numPerLine) *
                    infoRec->MonoPatternPitch;
            }
        }
        infoRec->CanDoMono8x8 = TRUE;
    }
    if (NumColor) {
        if (!(infoRec->Color8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {

            for (i = 0; i < 64; i++) {
                pCachePriv->ColorOffsets[i].y = i & 0x07;
                pCachePriv->ColorOffsets[i].x = i & ~0x07;
            }
        }
        infoRec->CanDoColor8x8 = TRUE;
    }

    if (!CACHEINIT(pScrn)) {
        xf86ErrorF("\tSetting up tile and stipple cache:\n");
        if (NumPartial)
            xf86ErrorF("\t\t%i %ix%i slots\n",
                       NumPartial, MaxPartialWidth, MaxPartialHeight);
        if (Num128)
            xf86ErrorF("\t\t%i 128x128 slots\n", Num128);
        if (Num256)
            xf86ErrorF("\t\t%i 256x256 slots\n", Num256);
        if (Num512)
            xf86ErrorF("\t\t%i 512x512 slots\n", Num512);
        if (NumColor)
            xf86ErrorF("\t\t%i 8x8 color pattern slots\n", NumColor);
        if (NumMono)
            xf86ErrorF("\t\t%i 8x8 color expansion slots\n", NumMono);
    }

    if (!(NumPartial | Num128 | Num256 | Num512 | NumColor | NumMono)) {
        if (!CACHEINIT(pScrn))
            xf86ErrorF("\t\tNot enough video memory for pixmap cache\n");
    }
    else
        infoRec->UsingPixmapCache = TRUE;

    CACHEINIT(pScrn) = 1;
}

#if X_BYTE_ORDER == X_BIG_ENDIAN
static CARD32 StippleMasks[4] = {
    0x80808080,
    0xC0C0C0C0,
    0x00000000,
    0xF0F0F0F0
};
#else
static CARD32 StippleMasks[4] = {
    0x01010101,
    0x03030303,
    0x00000000,
    0x0F0F0F0F
};
#endif

Bool
XAACheckStippleReducibility(PixmapPtr pPixmap)
{
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPixmap);
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_DRAWABLE(&pPixmap->drawable);
    CARD32 *IntPtr = (CARD32 *) pPixmap->devPrivate.ptr;
    int w = pPixmap->drawable.width;
    int h = pPixmap->drawable.height;
    int i;
    CARD32 bits[8];
    CARD32 mask = SHIFT_R(0xFFFFFFFF, 24);

    pPriv->flags |= REDUCIBILITY_CHECKED | REDUCIBLE_TO_2_COLOR;
    pPriv->flags &= ~REDUCIBLE_TO_8x8;

    if ((w > 32) || (h > 32) || (w & (w - 1)) || (h & (h - 1)))
        return FALSE;

    i = (h > 8) ? 8 : h;

    switch (w) {
    case 32:
        while (i--) {
            bits[i] = IntPtr[i] & mask;
            if ((bits[i] != SHIFT_R((IntPtr[i] & SHIFT_L(mask, 8)), 8)) ||
                (bits[i] != SHIFT_R((IntPtr[i] & SHIFT_L(mask, 16)), 16)) ||
                (bits[i] != SHIFT_R((IntPtr[i] & SHIFT_L(mask, 24)), 24)))
                return FALSE;
        }
        break;
    case 16:
        while (i--) {
            bits[i] = IntPtr[i] & mask;
            if (bits[i] != ((IntPtr[i] & SHIFT_R(SHIFT_L(mask, 8), 8))))
                return FALSE;
        }
        break;
    default:
        while (i--)
            bits[i] = IntPtr[i] & mask;
        break;
    }

    switch (h) {
    case 32:
        if ((IntPtr[8] != IntPtr[16]) || (IntPtr[9] != IntPtr[17]) ||
            (IntPtr[10] != IntPtr[18]) || (IntPtr[11] != IntPtr[19]) ||
            (IntPtr[12] != IntPtr[20]) || (IntPtr[13] != IntPtr[21]) ||
            (IntPtr[14] != IntPtr[22]) || (IntPtr[15] != IntPtr[23]) ||
            (IntPtr[16] != IntPtr[24]) || (IntPtr[17] != IntPtr[25]) ||
            (IntPtr[18] != IntPtr[26]) || (IntPtr[19] != IntPtr[27]) ||
            (IntPtr[20] != IntPtr[28]) || (IntPtr[21] != IntPtr[29]) ||
            (IntPtr[22] != IntPtr[30]) || (IntPtr[23] != IntPtr[31]))
            return FALSE;
        /* fall through */
    case 16:
        if ((IntPtr[0] != IntPtr[8]) || (IntPtr[1] != IntPtr[9]) ||
            (IntPtr[2] != IntPtr[10]) || (IntPtr[3] != IntPtr[11]) ||
            (IntPtr[4] != IntPtr[12]) || (IntPtr[5] != IntPtr[13]) ||
            (IntPtr[6] != IntPtr[14]) || (IntPtr[7] != IntPtr[15]))
            return FALSE;
    case 8:
        break;
    case 1:
        bits[1] = bits[0];
    case 2:
        bits[2] = bits[0];
        bits[3] = bits[1];
    case 4:
        bits[4] = bits[0];
        bits[5] = bits[1];
        bits[6] = bits[2];
        bits[7] = bits[3];
        break;
    }

    pPriv->flags |= REDUCIBLE_TO_8x8;

    pPriv->pattern0 =
        bits[0] | SHIFT_L(bits[1], 8) | SHIFT_L(bits[2], 16) | SHIFT_L(bits[3],
                                                                       24);
    pPriv->pattern1 =
        bits[4] | SHIFT_L(bits[5], 8) | SHIFT_L(bits[6], 16) | SHIFT_L(bits[7],
                                                                       24);

    if (w < 8) {
        pPriv->pattern0 &= StippleMasks[w - 1];
        pPriv->pattern1 &= StippleMasks[w - 1];

        switch (w) {
        case 1:
            pPriv->pattern0 |= SHIFT_L(pPriv->pattern0, 1);
            pPriv->pattern1 |= SHIFT_L(pPriv->pattern1, 1);
        case 2:
            pPriv->pattern0 |= SHIFT_L(pPriv->pattern0, 2);
            pPriv->pattern1 |= SHIFT_L(pPriv->pattern1, 2);
        case 4:
            pPriv->pattern0 |= SHIFT_L(pPriv->pattern0, 4);
            pPriv->pattern1 |= SHIFT_L(pPriv->pattern1, 4);
        }
    }

    if (infoRec->Mono8x8PatternFillFlags & BIT_ORDER_IN_BYTE_MSBFIRST) {
        pPriv->pattern0 = SWAP_BITS_IN_BYTES(pPriv->pattern0);
        pPriv->pattern1 = SWAP_BITS_IN_BYTES(pPriv->pattern1);
    }

    return TRUE;
}

Bool
XAACheckTileReducibility(PixmapPtr pPixmap, Bool checkMono)
{
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPixmap);
    CARD32 *IntPtr;
    int w = pPixmap->drawable.width;
    int h = pPixmap->drawable.height;
    int pitch = pPixmap->devKind >> 2;
    int dwords, i, j;

    pPriv->flags |= REDUCIBILITY_CHECKED;
    pPriv->flags &= ~(REDUCIBILITY_CHECKED | REDUCIBLE_TO_2_COLOR);

    if ((w > 32) || (h > 32) || (w & (w - 1)) || (h & (h - 1)))
        return FALSE;

    dwords = ((w * pPixmap->drawable.bitsPerPixel) + 31) >> 5;
    i = (h > 8) ? 8 : h;

    if (w > 8) {
        IntPtr = (CARD32 *) pPixmap->devPrivate.ptr;
        switch (pPixmap->drawable.bitsPerPixel) {
        case 8:
            while (i--) {
                for (j = 2; j < dwords; j++)
                    if (IntPtr[j] != IntPtr[j & 0x01])
                        return FALSE;
                IntPtr += pitch;
            }
            break;
        case 16:
            while (i--) {
                for (j = 4; j < dwords; j++)
                    if (IntPtr[j] != IntPtr[j & 0x03])
                        return FALSE;
                IntPtr += pitch;
            }
            break;
        case 24:
            while (i--) {
                for (j = 6; j < dwords; j++)
                    if (IntPtr[j] != IntPtr[j % 6])
                        return FALSE;
                IntPtr += pitch;
            }
            break;
        case 32:
            while (i--) {
                for (j = 8; j < dwords; j++)
                    if (IntPtr[j] != IntPtr[j & 0x07])
                        return FALSE;
                IntPtr += pitch;
            }
            break;
        default:
            return FALSE;
        }

    }

    if (h == 32) {
        CARD32 *IntPtr2, *IntPtr3, *IntPtr4;

        i = 8;
        IntPtr = (CARD32 *) pPixmap->devPrivate.ptr;
        IntPtr2 = IntPtr + (pitch << 3);
        IntPtr3 = IntPtr2 + (pitch << 3);
        IntPtr4 = IntPtr3 + (pitch << 3);
        while (i--) {
            for (j = 0; j < dwords; j++)
                if ((IntPtr[j] != IntPtr2[j]) || (IntPtr[j] != IntPtr3[j]) ||
                    (IntPtr[j] != IntPtr4[j]))
                    return FALSE;
            IntPtr += pitch;
            IntPtr2 += pitch;
            IntPtr3 += pitch;
            IntPtr4 += pitch;
        }
    }
    else if (h == 16) {
        CARD32 *IntPtr2;

        i = 8;
        IntPtr = (CARD32 *) pPixmap->devPrivate.ptr;
        IntPtr2 = IntPtr + (pitch << 3);
        while (i--) {
            for (j = 0; j < dwords; j++)
                if (IntPtr[j] != IntPtr2[j])
                    return FALSE;
            IntPtr += pitch;
            IntPtr2 += pitch;
        }
    }

    pPriv->flags |= REDUCIBLE_TO_8x8;

    if (checkMono) {
        XAAInfoRecPtr infoRec =
            GET_XAAINFORECPTR_FROM_DRAWABLE(&pPixmap->drawable);
        unsigned char bits[8];
        int fg, bg = -1, x, y;

        i = (h > 8) ? 8 : h;
        j = (w > 8) ? 8 : w;

        if (pPixmap->drawable.bitsPerPixel == 8) {
            unsigned char *srcp = pPixmap->devPrivate.ptr;

            fg = srcp[0];
            pitch = pPixmap->devKind;
            for (y = 0; y < i; y++) {
                bits[y] = 0;
                for (x = 0; x < j; x++) {
                    if (srcp[x] != fg) {
                        if (bg == -1)
                            bg = srcp[x];
                        else if (bg != srcp[x])
                            return TRUE;
                    }
                    else
                        bits[y] |= 1 << x;
                }
                srcp += pitch;
            }
        }
        else if (pPixmap->drawable.bitsPerPixel == 16) {
            unsigned short *srcp = (unsigned short *) pPixmap->devPrivate.ptr;

            fg = srcp[0];
            pitch = pPixmap->devKind >> 1;
            for (y = 0; y < i; y++) {
                bits[y] = 0;
                for (x = 0; x < j; x++) {
                    if (srcp[x] != fg) {
                        if (bg == -1)
                            bg = srcp[x];
                        else if (bg != srcp[x])
                            return TRUE;
                    }
                    else
                        bits[y] |= 1 << x;
                }
                srcp += pitch;
            }
        }
        else if (pPixmap->drawable.bitsPerPixel == 24) {
            CARD32 val;
            unsigned char *srcp = pPixmap->devPrivate.ptr;

            fg = *((CARD32 *) srcp) & 0x00FFFFFF;
            pitch = pPixmap->devKind;
            j *= 3;
            for (y = 0; y < i; y++) {
                bits[y] = 0;
                for (x = 0; x < j; x += 3) {
                    val = *((CARD32 *) (srcp + x)) & 0x00FFFFFF;
                    if (val != fg) {
                        if (bg == -1)
                            bg = val;
                        else if (bg != val)
                            return TRUE;
                    }
                    else
                        bits[y] |= 1 << (x / 3);
                }
                srcp += pitch;
            }
        }
        else if (pPixmap->drawable.bitsPerPixel == 32) {
            IntPtr = (CARD32 *) pPixmap->devPrivate.ptr;
            fg = IntPtr[0];
            for (y = 0; y < i; y++) {
                bits[y] = 0;
                for (x = 0; x < j; x++) {
                    if (IntPtr[x] != fg) {
                        if (bg == -1)
                            bg = IntPtr[x];
                        else if (bg != IntPtr[x])
                            return TRUE;
                    }
                    else
                        bits[y] |= 1 << x;
                }
                IntPtr += pitch;
            }
        }
        else
            return TRUE;

        pPriv->fg = fg;
        if (bg == -1)
            pPriv->bg = fg;
        else
            pPriv->bg = bg;

        if (h < 8) {
            switch (h) {
            case 1:
                bits[1] = bits[0];
            case 2:
                bits[2] = bits[0];
                bits[3] = bits[1];
            case 4:
                bits[4] = bits[0];
                bits[5] = bits[1];
                bits[6] = bits[2];
                bits[7] = bits[3];
                break;
            }
        }

        pPriv->pattern0 =
            bits[0] | (bits[1] << 8) | (bits[2] << 16) | (bits[3] << 24);
        pPriv->pattern1 =
            bits[4] | (bits[5] << 8) | (bits[6] << 16) | (bits[7] << 24);

        if (w < 8) {
            switch (w) {
            case 1:
                pPriv->pattern0 |= (pPriv->pattern0 << 1);
                pPriv->pattern1 |= (pPriv->pattern1 << 1);
            case 2:
                pPriv->pattern0 |= (pPriv->pattern0 << 2);
                pPriv->pattern1 |= (pPriv->pattern1 << 2);
            case 4:
                pPriv->pattern0 |= (pPriv->pattern0 << 4);
                pPriv->pattern1 |= (pPriv->pattern1 << 4);
            }
        }
        pPriv->flags |= REDUCIBLE_TO_2_COLOR;

        if (infoRec->Mono8x8PatternFillFlags & BIT_ORDER_IN_BYTE_MSBFIRST) {
            pPriv->pattern0 = SWAP_BITS_IN_BYTES(pPriv->pattern0);
            pPriv->pattern1 = SWAP_BITS_IN_BYTES(pPriv->pattern1);
        }

    }

    return TRUE;
}

void
XAATileCache(ScrnInfoPtr pScrn, XAACacheInfoPtr pCache, int w, int h)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SetupForScreenToScreenCopy) (pScrn, 1, 1, GXcopy, ~0, -1);

    while ((w << 1) <= pCache->w) {
        (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pCache->x, pCache->y,
                                                  pCache->x + w, pCache->y, w,
                                                  h);
        w <<= 1;
    }
    if (w != pCache->w) {
        (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pCache->x, pCache->y,
                                                  pCache->x + w, pCache->y,
                                                  pCache->w - w, h);
        w = pCache->w;
    }

    while ((h << 1) <= pCache->h) {
        (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pCache->x, pCache->y,
                                                  pCache->x, pCache->y + h, w,
                                                  h);
        h <<= 1;
    }
    if (h != pCache->h) {
        (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pCache->x, pCache->y,
                                                  pCache->x, pCache->y + h, w,
                                                  pCache->h - h);
    }
    SET_SYNC_FLAG(infoRec);
}

XAACacheInfoPtr
XAACacheTile(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    int w = pPix->drawable.width;
    int h = pPix->drawable.height;
    int size = max(w, h);
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache, cacheRoot = NULL;
    int i, max = 0;
    int *current;

    if (size <= 128) {
        if (pCachePriv->Info128) {
            cacheRoot = pCachePriv->Info128;
            max = pCachePriv->Num128x128;
            current = &pCachePriv->Current128;
        }
        else {
            cacheRoot = pCachePriv->InfoPartial;
            max = pCachePriv->NumPartial;
            current = &pCachePriv->CurrentPartial;
        }
    }
    else if (size <= 256) {
        cacheRoot = pCachePriv->Info256;
        max = pCachePriv->Num256x256;
        current = &pCachePriv->Current256;
    }
    else if (size <= 512) {
        cacheRoot = pCachePriv->Info512;
        max = pCachePriv->Num512x512;
        current = &pCachePriv->Current512;
    }
    else {                      /* something's wrong */
        ErrorF("Something's wrong in XAACacheTile()\n");
        return pCachePriv->Info128;
    }

    pCache = cacheRoot;

    /* lets look for it */
    for (i = 0; i < max; i++, pCache++) {
        if (pCache->serialNumber == pPix->drawable.serialNumber) {
            pCache->trans_color = -1;
            return pCache;
        }
    }

    pCache = &cacheRoot[(*current)++];
    if (*current >= max)
        *current = 0;

    pCache->serialNumber = pPix->drawable.serialNumber;
    pCache->trans_color = pCache->bg = pCache->fg = -1;
    pCache->orig_w = w;
    pCache->orig_h = h;
    (*infoRec->WritePixmapToCache) (pScrn, pCache->x, pCache->y, w, h,
                                    pPix->devPrivate.ptr, pPix->devKind,
                                    pPix->drawable.bitsPerPixel,
                                    pPix->drawable.depth);
    if (!(infoRec->PixmapCacheFlags & DO_NOT_TILE_COLOR_DATA) &&
        ((w != pCache->w) || (h != pCache->h)))
        XAATileCache(pScrn, pCache, w, h);

    return pCache;
}

XAACacheInfoPtr
XAACacheMonoStipple(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    int w = pPix->drawable.width;
    int h = pPix->drawable.height;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache, cacheRoot = NULL;
    int i, max = 0, funcNo, pad, dwords, bpp = pScrn->bitsPerPixel;
    int *current;
    StippleScanlineProcPtr StippleFunc;
    unsigned char *data, *srcPtr, *dstPtr;

    if ((h <= 128) && (w <= 128 * bpp)) {
        if (pCachePriv->Info128) {
            cacheRoot = pCachePriv->Info128;
            max = pCachePriv->Num128x128;
            current = &pCachePriv->Current128;
        }
        else {
            cacheRoot = pCachePriv->InfoPartial;
            max = pCachePriv->NumPartial;
            current = &pCachePriv->CurrentPartial;
        }
    }
    else if ((h <= 256) && (w <= 256 * bpp)) {
        cacheRoot = pCachePriv->Info256;
        max = pCachePriv->Num256x256;
        current = &pCachePriv->Current256;
    }
    else if ((h <= 512) && (w <= 526 * bpp)) {
        cacheRoot = pCachePriv->Info512;
        max = pCachePriv->Num512x512;
        current = &pCachePriv->Current512;
    }
    else {                      /* something's wrong */
        ErrorF("Something's wrong in XAACacheMonoStipple()\n");
        return pCachePriv->Info128;
    }

    pCache = cacheRoot;

    /* lets look for it */
    for (i = 0; i < max; i++, pCache++) {
        if ((pCache->serialNumber == pPix->drawable.serialNumber) &&
            (pCache->fg == -1) && (pCache->bg == -1)) {
            pCache->trans_color = -1;
            return pCache;
        }
    }

    pCache = &cacheRoot[(*current)++];
    if (*current >= max)
        *current = 0;

    pCache->serialNumber = pPix->drawable.serialNumber;
    pCache->trans_color = pCache->bg = pCache->fg = -1;
    pCache->orig_w = w;
    pCache->orig_h = h;

    if (w <= 32) {
        if (w & (w - 1))
            funcNo = 1;
        else
            funcNo = 0;
    }
    else
        funcNo = 2;

    pad = BitmapBytePad(pCache->w * bpp);
    dwords = bytes_to_int32(pad);
    dstPtr = data = (unsigned char *) malloc(pad * pCache->h);
    srcPtr = (unsigned char *) pPix->devPrivate.ptr;

    if (infoRec->
        ScreenToScreenColorExpandFillFlags & BIT_ORDER_IN_BYTE_MSBFIRST)
        StippleFunc = XAAStippleScanlineFuncMSBFirst[funcNo];
    else
        StippleFunc = XAAStippleScanlineFuncLSBFirst[funcNo];

    /* don't bother generating more than we'll ever use */
    max = ((pScrn->displayWidth + w - 1) + 31) >> 5;
    if (dwords > max)
        dwords = max;

    for (i = 0; i < h; i++) {
        (*StippleFunc) ((CARD32 *) dstPtr, (CARD32 *) srcPtr, 0, w, dwords);
        srcPtr += pPix->devKind;
        dstPtr += pad;
    }

    while ((h << 1) <= pCache->h) {
        memcpy(data + (pad * h), data, pad * h);
        h <<= 1;
    }

    if (h < pCache->h)
        memcpy(data + (pad * h), data, pad * (pCache->h - h));

    (*infoRec->WritePixmapToCache) (pScrn, pCache->x, pCache->y, pCache->w,
                                    pCache->h, data, pad, bpp, pScrn->depth);

    free(data);

    return pCache;
}

XAACacheInfoPtr
XAACachePlanarMonoStipple(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    int w = pPix->drawable.width;
    int h = pPix->drawable.height;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache, cacheRoot = NULL;
    int i, max = 0;
    int *current;

    if ((h <= 128) && (w <= 128)) {
        if (pCachePriv->Info128) {
            cacheRoot = pCachePriv->Info128;
            max = pCachePriv->Num128x128;
            current = &pCachePriv->Current128;
        }
        else {
            cacheRoot = pCachePriv->InfoPartial;
            max = pCachePriv->NumPartial;
            current = &pCachePriv->CurrentPartial;
        }
    }
    else if ((h <= 256) && (w <= 256)) {
        cacheRoot = pCachePriv->Info256;
        max = pCachePriv->Num256x256;
        current = &pCachePriv->Current256;
    }
    else if ((h <= 512) && (w <= 526)) {
        cacheRoot = pCachePriv->Info512;
        max = pCachePriv->Num512x512;
        current = &pCachePriv->Current512;
    }
    else {                      /* something's wrong */
        ErrorF("Something's wrong in XAACachePlanarMonoStipple()\n");
        return pCachePriv->Info128;
    }

    pCache = cacheRoot;

    /* lets look for it */
    for (i = 0; i < max; i++, pCache++) {
        if ((pCache->serialNumber == pPix->drawable.serialNumber) &&
            (pCache->fg == -1) && (pCache->bg == -1)) {
            pCache->trans_color = -1;
            return pCache;
        }
    }

    pCache = &cacheRoot[(*current)++];
    if (*current >= max)
        *current = 0;

    pCache->serialNumber = pPix->drawable.serialNumber;
    pCache->trans_color = pCache->bg = pCache->fg = -1;
    pCache->orig_w = w;
    pCache->orig_h = h;

    /* Plane 0 holds the stipple. Plane 1 holds the inverted stipple */
    (*infoRec->WriteBitmapToCache) (pScrn, pCache->x, pCache->y,
                                    pPix->drawable.width, pPix->drawable.height,
                                    pPix->devPrivate.ptr, pPix->devKind, 1, 2);
    if (!(infoRec->PixmapCacheFlags & DO_NOT_TILE_MONO_DATA) &&
        ((w != pCache->w) || (h != pCache->h)))
        XAATileCache(pScrn, pCache, w, h);

    return pCache;
}

XAACachePlanarMonoStippleProc
XAAGetCachePlanarMonoStipple(void)
{
    return XAACachePlanarMonoStipple;
}

XAACacheInfoPtr
XAACacheStipple(ScrnInfoPtr pScrn, PixmapPtr pPix, int fg, int bg)
{
    int w = pPix->drawable.width;
    int h = pPix->drawable.height;
    int size = max(w, h);
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache, cacheRoot = NULL;
    int i, max = 0;
    int *current;

    if (size <= 128) {
        if (pCachePriv->Info128) {
            cacheRoot = pCachePriv->Info128;
            max = pCachePriv->Num128x128;
            current = &pCachePriv->Current128;
        }
        else {
            cacheRoot = pCachePriv->InfoPartial;
            max = pCachePriv->NumPartial;
            current = &pCachePriv->CurrentPartial;
        }
    }
    else if (size <= 256) {
        cacheRoot = pCachePriv->Info256;
        max = pCachePriv->Num256x256;
        current = &pCachePriv->Current256;
    }
    else if (size <= 512) {
        cacheRoot = pCachePriv->Info512;
        max = pCachePriv->Num512x512;
        current = &pCachePriv->Current512;
    }
    else {                      /* something's wrong */
        ErrorF("Something's wrong in XAACacheStipple()\n");
        return pCachePriv->Info128;
    }

    pCache = cacheRoot;
    /* lets look for it */
    if (bg == -1)
        for (i = 0; i < max; i++, pCache++) {
            if ((pCache->serialNumber == pPix->drawable.serialNumber) &&
                (fg == pCache->fg) && (pCache->fg != pCache->bg)) {
                pCache->trans_color = pCache->bg;
                return pCache;
            }
        }
    else
        for (i = 0; i < max; i++, pCache++) {
            if ((pCache->serialNumber == pPix->drawable.serialNumber) &&
                (fg == pCache->fg) && (bg == pCache->bg)) {
                pCache->trans_color = -1;
                return pCache;
            }
        }

    pCache = &cacheRoot[(*current)++];
    if (*current >= max)
        *current = 0;

    pCache->serialNumber = pPix->drawable.serialNumber;
    pCache->fg = fg;
    if (bg == -1)
        pCache->trans_color = bg = fg ^ 1;
    else
        pCache->trans_color = -1;
    pCache->bg = bg;

    pCache->orig_w = w;
    pCache->orig_h = h;
    (*infoRec->WriteBitmapToCache) (pScrn, pCache->x, pCache->y,
                                    pPix->drawable.width, pPix->drawable.height,
                                    pPix->devPrivate.ptr, pPix->devKind, fg,
                                    bg);
    if (!(infoRec->PixmapCacheFlags & DO_NOT_TILE_COLOR_DATA) &&
        ((w != pCache->w) || (h != pCache->h)))
        XAATileCache(pScrn, pCache, w, h);

    return pCache;
}

XAACacheInfoPtr
XAACacheMono8x8Pattern(ScrnInfoPtr pScrn, int pat0, int pat1)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache = pCachePriv->InfoMono;
    int i;

    for (i = 0; i < pCachePriv->NumMono; i++, pCache++) {
        if (pCache->serialNumber &&
            (pCache->pat0 == pat0) && (pCache->pat1 == pat1))
            return pCache;
    }

    /* OK, let's cache it */
    pCache = &pCachePriv->InfoMono[pCachePriv->CurrentMono++];
    if (pCachePriv->CurrentMono >= pCachePriv->NumMono)
        pCachePriv->CurrentMono = 0;

    pCache->serialNumber = 1;   /* we don't care since we do lookups by pattern */
    pCache->pat0 = pat0;
    pCache->pat1 = pat1;

    (*infoRec->WriteMono8x8PatternToCache) (pScrn, pCache);

    return pCache;
}

XAACacheInfoPtr
XAACacheColor8x8Pattern(ScrnInfoPtr pScrn, PixmapPtr pPix, int fg, int bg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    XAACacheInfoPtr pCache = pCachePriv->InfoColor;
    XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
    int i;

    if (!(pixPriv->flags & REDUCIBLE_TO_2_COLOR)) {
        for (i = 0; i < pCachePriv->NumColor; i++, pCache++) {
            if (pCache->serialNumber == pPix->drawable.serialNumber) {
                pCache->trans_color = -1;
                return pCache;
            }
        }
        pCache = &pCachePriv->InfoColor[pCachePriv->CurrentColor++];
        if (pCachePriv->CurrentColor >= pCachePriv->NumColor)
            pCachePriv->CurrentColor = 0;

        pCache->serialNumber = pPix->drawable.serialNumber;
        pCache->trans_color = pCache->fg = pCache->bg = -1;
    }
    else {
        int pat0 = pixPriv->pattern0;
        int pat1 = pixPriv->pattern1;

        if (fg == -1) {         /* it's a tile */
            fg = pixPriv->fg;
            bg = pixPriv->bg;
        }

        if (bg == -1) {         /* stipple */
            for (i = 0; i < pCachePriv->NumColor; i++, pCache++) {
                if (pCache->serialNumber &&
                    (pCache->pat0 == pat0) && (pCache->pat1 == pat1) &&
                    (pCache->fg == fg) && (pCache->bg != fg)) {
                    pCache->trans_color = pCache->bg;
                    return pCache;
                }
            }
        }
        else {                  /* opaque stipple */
            for (i = 0; i < pCachePriv->NumColor; i++, pCache++) {
                if (pCache->serialNumber &&
                    (pCache->pat0 == pat0) && (pCache->pat1 == pat1) &&
                    (pCache->fg == fg) && (pCache->bg == bg)) {
                    pCache->trans_color = -1;
                    return pCache;
                }
            }
        }
        pCache = &pCachePriv->InfoColor[pCachePriv->CurrentColor++];
        if (pCachePriv->CurrentColor >= pCachePriv->NumColor)
            pCachePriv->CurrentColor = 0;

        if (bg == -1)
            pCache->trans_color = bg = fg ^ 1;
        else
            pCache->trans_color = -1;

        pCache->pat0 = pat0;
        pCache->pat1 = pat1;
        pCache->fg = fg;
        pCache->bg = bg;
        pCache->serialNumber = 1;
    }

    (*infoRec->WriteColor8x8PatternToCache) (pScrn, pPix, pCache);

    return pCache;
}

void
XAAWriteBitmapToCache(ScrnInfoPtr pScrn,
                      int x, int y, int w, int h,
                      unsigned char *src, int srcwidth, int fg, int bg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->WriteBitmap) (pScrn, x, y, w, h, src, srcwidth,
                             0, fg, bg, GXcopy, ~0);
}

void
XAAWriteBitmapToCacheLinear(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h,
                            unsigned char *src, int srcwidth, int fg, int bg)
{
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pScreenPix, pDstPix;
    ChangeGCVal gcvals[2];
    GCPtr pGC;

    pScreenPix = (*pScreen->GetScreenPixmap) (pScreen);

    pDstPix = GetScratchPixmapHeader(pScreen, pScreenPix->drawable.width,
                                     y + h, pScreenPix->drawable.depth,
                                     pScreenPix->drawable.bitsPerPixel,
                                     pScreenPix->devKind,
                                     pScreenPix->devPrivate.ptr);

    pGC = GetScratchGC(pScreenPix->drawable.depth, pScreen);
    gcvals[0].val = fg;
    gcvals[1].val = bg;
    ChangeGC(NullClient, pGC, GCForeground | GCBackground, gcvals);
    ValidateGC((DrawablePtr) pDstPix, pGC);

    /* We've unwrapped already so these ops miss a sync */
    SYNC_CHECK(pScrn);

    (*pGC->ops->PutImage) ((DrawablePtr) pDstPix, pGC, 1, x, y, w, h, 0,
                           XYBitmap, (pointer) src);

    FreeScratchGC(pGC);
    FreeScratchPixmapHeader(pDstPix);
}

void
XAAWritePixmapToCache(ScrnInfoPtr pScrn,
                      int x, int y, int w, int h,
                      unsigned char *src, int srcwidth, int bpp, int depth)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->WritePixmap) (pScrn, x, y, w, h, src, srcwidth,
                             GXcopy, ~0, -1, bpp, depth);
}

void
XAAWritePixmapToCacheLinear(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h,
                            unsigned char *src,
                            int srcwidth, int bpp, int depth)
{
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pScreenPix, pDstPix;
    GCPtr pGC;

    pScreenPix = (*pScreen->GetScreenPixmap) (pScreen);

    pDstPix = GetScratchPixmapHeader(pScreen, x + w, y + h,
                                     depth, bpp, pScreenPix->devKind,
                                     pScreenPix->devPrivate.ptr);

    pGC = GetScratchGC(depth, pScreen);
    ValidateGC((DrawablePtr) pDstPix, pGC);

    /* We've unwrapped already so these ops miss a sync */
    SYNC_CHECK(pScrn);

    if (bpp == BitsPerPixel(depth))
        (*pGC->ops->PutImage) ((DrawablePtr) pDstPix, pGC, depth, x, y, w,
                               h, 0, ZPixmap, (pointer) src);
    else {
        PixmapPtr pSrcPix;

        pSrcPix = GetScratchPixmapHeader(pScreen, w, h, depth, bpp,
                                         srcwidth, (pointer) src);

        (*pGC->ops->CopyArea) ((DrawablePtr) pSrcPix, (DrawablePtr) pDstPix,
                               pGC, 0, 0, w, h, x, y);

        FreeScratchPixmapHeader(pSrcPix);
    }

    FreeScratchGC(pGC);
    FreeScratchPixmapHeader(pDstPix);
}

void
XAAWriteMono8x8PatternToCache(ScrnInfoPtr pScrn, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    unsigned char *data;
    int pad, Bpp = (pScrn->bitsPerPixel >> 3);

    pCache->offsets = pCachePriv->MonoOffsets;

    pad = BitmapBytePad(pCache->w * pScrn->bitsPerPixel);

    data = (unsigned char *) malloc(pad * pCache->h);
    if (!data)
        return;

    if (infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_ORIGIN) {
        CARD32 *ptr = (CARD32 *) data;

        ptr[0] = pCache->pat0;
        ptr[1] = pCache->pat1;
    }
    else {
        CARD32 *ptr;
        DDXPointPtr pPoint = pCache->offsets;
        int patx, paty, i;

        for (i = 0; i < 64; i++, pPoint++) {
            patx = pCache->pat0;
            paty = pCache->pat1;
            XAARotateMonoPattern(&patx, &paty, i & 0x07, i >> 3,
                                 (infoRec->Mono8x8PatternFillFlags &
                                  BIT_ORDER_IN_BYTE_MSBFIRST));
            ptr = (CARD32 *) (data + (pad * pPoint->y) + (Bpp * pPoint->x));
            ptr[0] = patx;
            ptr[1] = paty;
        }
    }

    (*infoRec->WritePixmapToCache) (pScrn, pCache->x, pCache->y,
                                    pCache->w, pCache->h, data, pad,
                                    pScrn->bitsPerPixel, pScrn->depth);

    free(data);
}

void
XAAWriteColor8x8PatternToCache(ScrnInfoPtr pScrn,
                               PixmapPtr pPix, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    int pad, i, w, h, nw, nh, Bpp;
    unsigned char *data, *srcPtr, *dstPtr;

    pCache->offsets = pCachePriv->ColorOffsets;

    if (pixPriv->flags & REDUCIBLE_TO_2_COLOR) {
        CARD32 *ptr;

        pad = BitmapBytePad(pCache->w);
        data = (unsigned char *) malloc(pad * pCache->h);
        if (!data)
            return;

        if (infoRec->Color8x8PatternFillFlags &
            HARDWARE_PATTERN_PROGRAMMED_ORIGIN) {
            ptr = (CARD32 *) data;
            ptr[0] = pCache->pat0;
            ptr[1] = pCache->pat1;
        }
        else {
            int patx, paty;

            ptr = (CARD32 *) data;
            ptr[0] = ptr[2] = pCache->pat0;
            ptr[1] = ptr[3] = pCache->pat1;
            for (i = 1; i < 8; i++) {
                patx = pCache->pat0;
                paty = pCache->pat1;
                XAARotateMonoPattern(&patx, &paty, i, 0,
                                     (infoRec->Mono8x8PatternFillFlags &
                                      BIT_ORDER_IN_BYTE_MSBFIRST));
                ptr = (CARD32 *) (data + (pad * i));
                ptr[0] = ptr[2] = patx;
                ptr[1] = ptr[3] = paty;
            }
        }

        (*infoRec->WriteBitmapToCache) (pScrn, pCache->x, pCache->y,
                                        pCache->w, pCache->h, data, pad,
                                        pCache->fg, pCache->bg);

        free(data);
        return;
    }

    Bpp = pScrn->bitsPerPixel >> 3;
    h = min(8, pPix->drawable.height);
    w = min(8, pPix->drawable.width);
    pad = BitmapBytePad(pCache->w * pScrn->bitsPerPixel);

    data = (unsigned char *) malloc(pad * pCache->h);
    if (!data)
        return;

    /* Write and expand horizontally. */
    for (i = h, dstPtr = data, srcPtr = pPix->devPrivate.ptr; i--;
         srcPtr += pPix->devKind, dstPtr += pScrn->bitsPerPixel) {
        nw = w;
        memcpy(dstPtr, srcPtr, w * Bpp);
        while (nw != 8) {
            memcpy(dstPtr + (nw * Bpp), dstPtr, nw * Bpp);
            nw <<= 1;
        }
    }
    nh = h;
    /* Expand vertically. */
    while (nh != 8) {
        memcpy(data + (nh * pScrn->bitsPerPixel), data,
               nh * pScrn->bitsPerPixel);
        nh <<= 1;
    }

    if (!(infoRec->Color8x8PatternFillFlags &
          HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
        int j;
        unsigned char *ptr = data + (128 * Bpp);

        memcpy(data + (64 * Bpp), data, 64 * Bpp);
        for (i = 1; i < 8; i++, ptr += (128 * Bpp)) {
            for (j = 0; j < 8; j++) {
                memcpy(ptr + (j * 8) * Bpp, data + (j * 8 + i) * Bpp,
                       (8 - i) * Bpp);
                memcpy(ptr + (j * 8 + 8 - i) * Bpp, data + j * 8 * Bpp,
                       i * Bpp);
            }
            memcpy(ptr + (64 * Bpp), ptr, 64 * Bpp);
        }
    }

    (*infoRec->WritePixmapToCache) (pScrn, pCache->x, pCache->y,
                                    pCache->w, pCache->h, data, pad,
                                    pScrn->bitsPerPixel, pScrn->depth);

    free(data);
}

int
XAAStippledFillChooser(GCPtr pGC)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    PixmapPtr pPixmap = pGC->stipple;
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPixmap);

    if (!(pPriv->flags & REDUCIBILITY_CHECKED) &&
        (infoRec->CanDoMono8x8 || infoRec->CanDoColor8x8)) {
        XAACheckStippleReducibility(pPixmap);
    }

    if (pPriv->flags & REDUCIBLE_TO_8x8) {
        if (infoRec->CanDoMono8x8 &&
            !(infoRec->FillMono8x8PatternSpansFlags & NO_TRANSPARENCY) &&
            ((pGC->alu == GXcopy) || !(infoRec->FillMono8x8PatternSpansFlags &
                                       TRANSPARENCY_GXCOPY_ONLY)) &&
            CHECK_ROP(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_FG(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_PLANEMASK(pGC, infoRec->FillMono8x8PatternSpansFlags)) {

            return DO_MONO_8x8;
        }

        if (infoRec->CanDoColor8x8 &&
            !(infoRec->FillColor8x8PatternSpansFlags & NO_TRANSPARENCY) &&
            ((pGC->alu == GXcopy) || !(infoRec->FillColor8x8PatternSpansFlags &
                                       TRANSPARENCY_GXCOPY_ONLY)) &&
            CHECK_ROP(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_PLANEMASK(pGC, infoRec->FillColor8x8PatternSpansFlags)) {

            return DO_COLOR_8x8;
        }
    }

    if (infoRec->UsingPixmapCache && infoRec->FillCacheExpandSpans &&
        (pPixmap->drawable.height <= infoRec->MaxCacheableStippleHeight) &&
        (pPixmap->drawable.width <= infoRec->MaxCacheableStippleWidth /
         infoRec->CacheColorExpandDensity) &&
        !(infoRec->FillCacheExpandSpansFlags & NO_TRANSPARENCY) &&
        ((pGC->alu == GXcopy) || !(infoRec->FillCacheExpandSpansFlags &
                                   TRANSPARENCY_GXCOPY_ONLY)) &&
        CHECK_ROP(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_FG(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheExpandSpansFlags)) {

        return DO_CACHE_EXPAND;
    }

    if (infoRec->UsingPixmapCache &&
        !(infoRec->PixmapCacheFlags & DO_NOT_BLIT_STIPPLES) &&
        infoRec->FillCacheBltSpans &&
        (pPixmap->drawable.height <= infoRec->MaxCacheableTileHeight) &&
        (pPixmap->drawable.width <= infoRec->MaxCacheableTileWidth) &&
        !(infoRec->FillCacheBltSpansFlags & NO_TRANSPARENCY) &&
        ((pGC->alu == GXcopy) || !(infoRec->FillCacheBltSpansFlags &
                                   TRANSPARENCY_GXCOPY_ONLY)) &&
        CHECK_ROP(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheBltSpansFlags)) {

        return DO_CACHE_BLT;
    }

    if (infoRec->FillColorExpandSpans &&
        !(infoRec->FillColorExpandSpansFlags & NO_TRANSPARENCY) &&
        ((pGC->alu == GXcopy) || !(infoRec->FillColorExpandSpansFlags &
                                   TRANSPARENCY_GXCOPY_ONLY)) &&
        CHECK_ROP(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_FG(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillColorExpandSpansFlags)) {

        return DO_COLOR_EXPAND;
    }

    return 0;
}

int
XAAOpaqueStippledFillChooser(GCPtr pGC)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    PixmapPtr pPixmap = pGC->stipple;
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPixmap);

    if (XAA_DEPTH_BUG(pGC))
        return 0;

    if (!(pPriv->flags & REDUCIBILITY_CHECKED) &&
        (infoRec->CanDoMono8x8 || infoRec->CanDoColor8x8)) {
        XAACheckStippleReducibility(pPixmap);
    }

    if (pPriv->flags & REDUCIBLE_TO_8x8) {
        if (infoRec->CanDoMono8x8 &&
            !(infoRec->FillMono8x8PatternSpansFlags & TRANSPARENCY_ONLY) &&
            CHECK_ROP(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_COLORS(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_PLANEMASK(pGC, infoRec->FillMono8x8PatternSpansFlags)) {

            return DO_MONO_8x8;
        }

        if (infoRec->CanDoColor8x8 &&
            CHECK_ROP(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_PLANEMASK(pGC, infoRec->FillColor8x8PatternSpansFlags)) {

            return DO_COLOR_8x8;
        }
    }

    if (infoRec->UsingPixmapCache && infoRec->FillCacheExpandSpans &&
        (pPixmap->drawable.height <= infoRec->MaxCacheableStippleHeight) &&
        (pPixmap->drawable.width <= infoRec->MaxCacheableStippleWidth /
         infoRec->CacheColorExpandDensity) &&
        !(infoRec->FillCacheExpandSpansFlags & TRANSPARENCY_ONLY) &&
        CHECK_ROP(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_COLORS(pGC, infoRec->FillCacheExpandSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheExpandSpansFlags)) {

        return DO_CACHE_EXPAND;
    }

    if (infoRec->UsingPixmapCache &&
        !(infoRec->PixmapCacheFlags & DO_NOT_BLIT_STIPPLES) &&
        infoRec->FillCacheBltSpans &&
        (pPixmap->drawable.height <= infoRec->MaxCacheableTileHeight) &&
        (pPixmap->drawable.width <= infoRec->MaxCacheableTileWidth) &&
        CHECK_ROP(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheBltSpansFlags)) {

        return DO_CACHE_BLT;
    }

    if (infoRec->FillColorExpandSpans &&
        !(infoRec->FillColorExpandSpansFlags & TRANSPARENCY_ONLY) &&
        CHECK_ROP(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_COLORS(pGC, infoRec->FillColorExpandSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillColorExpandSpansFlags)) {

        return DO_COLOR_EXPAND;
    }

    return 0;
}

int
XAATiledFillChooser(GCPtr pGC)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    PixmapPtr pPixmap = pGC->tile.pixmap;
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPixmap);

    if (IS_OFFSCREEN_PIXMAP(pPixmap) && infoRec->FillCacheBltSpans &&
        CHECK_ROP(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheBltSpansFlags)) {

        return DO_PIXMAP_COPY;
    }

    if (!(pPriv->flags & REDUCIBILITY_CHECKED) &&
        (infoRec->CanDoMono8x8 || infoRec->CanDoColor8x8)) {
        XAACheckTileReducibility(pPixmap, infoRec->CanDoMono8x8);
    }

    if (pPriv->flags & REDUCIBLE_TO_8x8) {
        if ((pPriv->flags & REDUCIBLE_TO_2_COLOR) && infoRec->CanDoMono8x8 &&
            !(infoRec->FillMono8x8PatternSpansFlags & TRANSPARENCY_ONLY) &&
            CHECK_ROP(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillMono8x8PatternSpansFlags) &&
            (!(infoRec->FillMono8x8PatternSpansFlags & RGB_EQUAL) ||
             (CHECK_RGB_EQUAL(pPriv->fg) && CHECK_RGB_EQUAL(pPriv->bg))) &&
            CHECK_PLANEMASK(pGC, infoRec->FillMono8x8PatternSpansFlags)) {

            return DO_MONO_8x8;
        }

        if (infoRec->CanDoColor8x8 &&
            CHECK_ROP(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillColor8x8PatternSpansFlags) &&
            CHECK_PLANEMASK(pGC, infoRec->FillColor8x8PatternSpansFlags)) {

            return DO_COLOR_8x8;
        }
    }

    if (infoRec->UsingPixmapCache && infoRec->FillCacheBltSpans &&
        (pPixmap->drawable.height <= infoRec->MaxCacheableTileHeight) &&
        (pPixmap->drawable.width <= infoRec->MaxCacheableTileWidth) &&
        CHECK_ROP(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillCacheBltSpansFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillCacheBltSpansFlags)) {

        return DO_CACHE_BLT;
    }

    if (infoRec->FillImageWriteRects &&
        CHECK_NO_GXCOPY(pGC, infoRec->FillImageWriteRectsFlags) &&
        CHECK_ROP(pGC, infoRec->FillImageWriteRectsFlags) &&
        CHECK_ROPSRC(pGC, infoRec->FillImageWriteRectsFlags) &&
        CHECK_PLANEMASK(pGC, infoRec->FillImageWriteRectsFlags)) {

        return DO_IMAGE_WRITE;
    }

    return 0;
}

static int RotateMasksX[8] = {
    0xFFFFFFFF, 0x7F7F7F7F, 0x3F3F3F3F, 0x1F1F1F1F,
    0x0F0F0F0F, 0x07070707, 0x03030303, 0x01010101
};

static int RotateMasksY[4] = {
    0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF
};

void
XAARotateMonoPattern(int *pat0, int *pat1, int xorg, int yorg, Bool msbfirst)
{
    int tmp, mask;

    if (xorg) {
        if (msbfirst)
            xorg = 8 - xorg;
        mask = RotateMasksX[xorg];
        *pat0 = ((*pat0 >> xorg) & mask) | ((*pat0 << (8 - xorg)) & ~mask);
        *pat1 = ((*pat1 >> xorg) & mask) | ((*pat1 << (8 - xorg)) & ~mask);
    }
    if (yorg >= 4) {
        tmp = *pat0;
        *pat0 = *pat1;
        *pat1 = tmp;
        yorg -= 4;
    }
    if (yorg) {
        mask = RotateMasksY[yorg];
        yorg <<= 3;
        tmp = *pat0;
        *pat0 = ((*pat0 >> yorg) & mask) | ((*pat1 << (32 - yorg)) & ~mask);
        *pat1 = ((*pat1 >> yorg) & mask) | ((tmp << (32 - yorg)) & ~mask);
    }
}

void
XAAInvalidatePixmapCache(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAPixmapCachePrivatePtr pCachePriv =
        (XAAPixmapCachePrivatePtr) infoRec->PixmapCachePrivate;
    int i;

    if (!pCachePriv)
        return;

    for (i = 0; i < pCachePriv->Num512x512; i++)
        (pCachePriv->Info512)[i].serialNumber = 0;
    for (i = 0; i < pCachePriv->Num256x256; i++)
        (pCachePriv->Info256)[i].serialNumber = 0;
    for (i = 0; i < pCachePriv->Num128x128; i++)
        (pCachePriv->Info128)[i].serialNumber = 0;
    for (i = 0; i < pCachePriv->NumPartial; i++)
        (pCachePriv->InfoPartial)[i].serialNumber = 0;
    for (i = 0; i < pCachePriv->NumMono; i++)
        (pCachePriv->InfoMono)[i].serialNumber = 0;
    for (i = 0; i < pCachePriv->NumColor; i++)
        (pCachePriv->InfoColor)[i].serialNumber = 0;
}
