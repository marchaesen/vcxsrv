
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "servermd.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "mi.h"
#include "pixmapstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"

void
XAAMoveDWORDS_FixedBase(register CARD32 *dest,
                        register CARD32 *src, register int dwords)
{
    while (dwords & ~0x03) {
        *dest = *src;
        *dest = *(src + 1);
        *dest = *(src + 2);
        *dest = *(src + 3);
        dwords -= 4;
        src += 4;
    }

    if (!dwords)
        return;
    *dest = *src;
    if (dwords == 1)
        return;
    *dest = *(src + 1);
    if (dwords == 2)
        return;
    *dest = *(src + 2);
}

void
XAAMoveDWORDS(register CARD32 *dest, register CARD32 *src, register int dwords)
{
    while (dwords & ~0x03) {
        *dest = *src;
        *(dest + 1) = *(src + 1);
        *(dest + 2) = *(src + 2);
        *(dest + 3) = *(src + 3);
        src += 4;
        dest += 4;
        dwords -= 4;
    }
    if (!dwords)
        return;
    *dest = *src;
    if (dwords == 1)
        return;
    *(dest + 1) = *(src + 1);
    if (dwords == 2)
        return;
    *(dest + 2) = *(src + 2);
}

void
XAAMoveDWORDS_FixedSrc(register CARD32 *dest,
                       register CARD32 *src, register int dwords)
{
    while (dwords & ~0x03) {
        *dest = *src;
        *(dest + 1) = *src;
        *(dest + 2) = *src;
        *(dest + 3) = *src;
        dest += 4;
        dwords -= 4;
    }
    if (!dwords)
        return;
    *dest = *src;
    if (dwords == 1)
        return;
    *(dest + 1) = *src;
    if (dwords == 2)
        return;
    *(dest + 2) = *src;
}

static void
XAAWritePixmap32To24(ScrnInfoPtr pScrn, int x, int y, int w, int h, unsigned char *srcInit, int srcwidth,       /* bytes */
                     int rop, unsigned int planemask, int trans)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int count, dwords = bytes_to_int32(w * 3);
    CARD32 *src, *dst;
    Bool PlusOne = FALSE;

    if ((infoRec->ImageWriteFlags & CPU_TRANSFER_PAD_QWORD) &&
        ((dwords * h) & 0x01)) {
        PlusOne = TRUE;
    }

    (*infoRec->SetupForImageWrite) (pScrn, rop, planemask, trans, 24, 24);
    (*infoRec->SubsequentImageWriteRect) (pScrn, x, y, w, h, 0);

    if (dwords > infoRec->ImageWriteRange) {
        dst = (CARD32 *) infoRec->ImageWriteBase;
        while (h--) {
            src = (CARD32 *) srcInit;
            count = w;

            while (count >= 4) {
                *dst = (src[0] & 0x00ffffff) | (src[1] << 24);
                *dst = ((src[1] >> 8) & 0x0000ffff) | (src[2] << 16);
                *dst = ((src[2] >> 16) & 0x000000ff) | (src[3] << 8);
                src += 4;
                count -= 4;
            }
            switch (count) {
            case 0:
                break;
            case 1:
                *dst = src[0];
                break;
            case 2:
                *dst = (src[0] & 0x00ffffff) | (src[1] << 24);
                *dst = src[1] >> 8;
                break;
            default:
                *dst = (src[0] & 0x00ffffff) | (src[1] << 24);
                *dst = ((src[1] >> 8) & 0x0000ffff) | (src[2] << 16);
                *dst = src[2] >> 16;
                break;
            }
            srcInit += srcwidth;
        }
    }
    else {
        while (h--) {
            dst = (CARD32 *) infoRec->ImageWriteBase;
            src = (CARD32 *) srcInit;
            count = w;

            while (count >= 4) {
                dst[0] = (src[0] & 0x00ffffff) | (src[1] << 24);
                dst[1] = ((src[1] >> 8) & 0x0000ffff) | (src[2] << 16);
                dst[2] = ((src[2] >> 16) & 0x000000ff) | (src[3] << 8);
                dst += 3;
                src += 4;
                count -= 4;
            }
            switch (count) {
            case 0:
                break;
            case 1:
                dst[0] = src[0];
                break;
            case 2:
                dst[0] = (src[0] & 0x00ffffff) | (src[1] << 24);
                dst[1] = src[1] >> 8;
                break;
            default:
                dst[0] = (src[0] & 0x00ffffff) | (src[1] << 24);
                dst[1] = ((src[1] >> 8) & 0x0000ffff) | (src[2] << 16);
                dst[2] = src[2] >> 16;
                break;
            }
            srcInit += srcwidth;
        }
    }

    if (PlusOne) {
        CARD32 *base = (CARD32 *) infoRec->ImageWriteBase;

        *base = 0x00000000;
    }

    if (infoRec->ImageWriteFlags & SYNC_AFTER_IMAGE_WRITE)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);

}

void
XAAWritePixmap(ScrnInfoPtr pScrn, int x, int y, int w, int h, unsigned char *src, int srcwidth, /* bytes */
               int rop, unsigned int planemask, int trans, int bpp, int depth)
{
    XAAInfoRecPtr infoRec;
    int dwords, skipleft, Bpp;
    Bool beCareful, PlusOne;

    if ((bpp == 32) && (pScrn->bitsPerPixel == 24)) {
        XAAWritePixmap32To24(pScrn, x, y, w, h, src, srcwidth,
                             rop, planemask, trans);
        return;
    }

    infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    beCareful = PlusOne = FALSE;
    Bpp = bpp >> 3;

    if ((skipleft = (long) src & 0x03L)) {
        if (!(infoRec->ImageWriteFlags & LEFT_EDGE_CLIPPING)) {
            skipleft = 0;
            beCareful = TRUE;
            goto BAD_ALIGNMENT;
        }

        if (Bpp == 3)
            skipleft = 4 - skipleft;
        else
            skipleft /= Bpp;

        if ((x < skipleft) && !(infoRec->ImageWriteFlags &
                                LEFT_EDGE_CLIPPING_NEGATIVE_X)) {
            skipleft = 0;
            beCareful = TRUE;
            goto BAD_ALIGNMENT;
        }

        x -= skipleft;
        w += skipleft;

        if (Bpp == 3)
            src -= 3 * skipleft;
        else                    /* is this Alpha friendly ? */
            src = (unsigned char *) ((long) src & ~0x03L);
    }

 BAD_ALIGNMENT:

    dwords = bytes_to_int32(w * Bpp);

    if ((infoRec->ImageWriteFlags & CPU_TRANSFER_PAD_QWORD) &&
        ((dwords * h) & 0x01)) {
        PlusOne = TRUE;
    }

    (*infoRec->SetupForImageWrite) (pScrn, rop, planemask, trans, bpp, depth);
    (*infoRec->SubsequentImageWriteRect) (pScrn, x, y, w, h, skipleft);

    if (beCareful) {
        /* in cases with bad alignment we have to be careful not
           to read beyond the end of the source */
        if (((x * Bpp) + (dwords << 2)) > srcwidth)
            h--;
        else
            beCareful = FALSE;
    }

    if (dwords > infoRec->ImageWriteRange) {
        while (h--) {
            XAAMoveDWORDS_FixedBase((CARD32 *) infoRec->ImageWriteBase,
                                    (CARD32 *) src, dwords);
            src += srcwidth;
        }
        if (beCareful) {
            int shift = ((long) src & 0x03L) << 3;

            if (--dwords)
                XAAMoveDWORDS_FixedBase((CARD32 *) infoRec->ImageWriteBase,
                                        (CARD32 *) src, dwords);
            src = (unsigned char *) ((long) (src + (dwords << 2)) & ~0x03L);
            *((CARD32 *) infoRec->ImageWriteBase) = *((CARD32 *) src) >> shift;
        }
    }
    else {
        if (srcwidth == (dwords << 2)) {
            int decrement = infoRec->ImageWriteRange / dwords;

            while (h > decrement) {
                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords * decrement);
                src += (srcwidth * decrement);
                h -= decrement;
            }
            if (h) {
                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords * h);
                if (beCareful)
                    src += (srcwidth * h);
            }
        }
        else {
            while (h--) {
                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords);
                src += srcwidth;
            }
        }

        if (beCareful) {
            int shift = ((long) src & 0x03L) << 3;

            if (--dwords)
                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords);
            src = (unsigned char *) ((long) (src + (dwords << 2)) & ~0x03L);

            ((CARD32 *) infoRec->ImageWriteBase)[dwords] =
                *((CARD32 *) src) >> shift;
        }
    }

    if (PlusOne) {
        CARD32 *base = (CARD32 *) infoRec->ImageWriteBase;

        *base = 0x00000000;
    }

    if (infoRec->ImageWriteFlags & SYNC_AFTER_IMAGE_WRITE)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

void
XAAWritePixmapScanline(ScrnInfoPtr pScrn, int x, int y, int w, int h, unsigned char *src, int srcwidth, /* bytes */
                       int rop,
                       unsigned int planemask, int trans, int bpp, int depth)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int dwords, skipleft, bufferNo = 0, Bpp = bpp >> 3;
    Bool beCareful = FALSE;
    CARD32 *base;

    if ((skipleft = (long) src & 0x03L)) {
        if (!(infoRec->ScanlineImageWriteFlags & LEFT_EDGE_CLIPPING)) {
            skipleft = 0;
            beCareful = TRUE;
            goto BAD_ALIGNMENT;
        }

        if (Bpp == 3)
            skipleft = 4 - skipleft;
        else
            skipleft /= Bpp;

        if ((x < skipleft) && !(infoRec->ScanlineImageWriteFlags &
                                LEFT_EDGE_CLIPPING_NEGATIVE_X)) {
            skipleft = 0;
            beCareful = TRUE;
            goto BAD_ALIGNMENT;
        }

        x -= skipleft;
        w += skipleft;

        if (Bpp == 3)
            src -= 3 * skipleft;
        else
            src = (unsigned char *) ((long) src & ~0x03L);
    }

 BAD_ALIGNMENT:

    dwords = bytes_to_int32(w * Bpp);

    (*infoRec->SetupForScanlineImageWrite) (pScrn, rop, planemask, trans, bpp,
                                            depth);
    (*infoRec->SubsequentScanlineImageWriteRect) (pScrn, x, y, w, h, skipleft);

    if (beCareful) {
        /* in cases with bad alignment we have to be careful not
           to read beyond the end of the source */
        if (((x * Bpp) + (dwords << 2)) > srcwidth)
            h--;
        else
            beCareful = FALSE;
    }

    while (h--) {
        base = (CARD32 *) infoRec->ScanlineImageWriteBuffers[bufferNo];
        XAAMoveDWORDS(base, (CARD32 *) src, dwords);
        (*infoRec->SubsequentImageWriteScanline) (pScrn, bufferNo++);
        src += srcwidth;
        if (bufferNo >= infoRec->NumScanlineImageWriteBuffers)
            bufferNo = 0;
    }

    if (beCareful) {
        int shift = ((long) src & 0x03L) << 3;

        base = (CARD32 *) infoRec->ScanlineImageWriteBuffers[bufferNo];
        if (--dwords)
            XAAMoveDWORDS(base, (CARD32 *) src, dwords);
        src = (unsigned char *) ((long) (src + (dwords << 2)) & ~0x03L);

        base[dwords] = *((CARD32 *) src) >> shift;
        (*infoRec->SubsequentImageWriteScanline) (pScrn, bufferNo);
    }

    SET_SYNC_FLAG(infoRec);
}

void
XAAPutImage(DrawablePtr pDraw,
            GCPtr pGC,
            int depth,
            int x, int y, int w, int h, int leftPad, int format, char *pImage)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int bpp = BitsPerPixel(depth);
    Bool depthBug = FALSE;

    if (!w || !h)
        return;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    depthBug = XAA_DEPTH_BUG(pGC);

    if (((format == ZPixmap) && infoRec->WritePixmap &&
         ((pDraw->bitsPerPixel == bpp) ||
          ((pDraw->bitsPerPixel == 24) && (bpp == 32) &&
           (infoRec->WritePixmapFlags & CONVERT_32BPP_TO_24BPP))) &&
         CHECK_ROP(pGC, infoRec->WritePixmapFlags) &&
         CHECK_ROPSRC(pGC, infoRec->WritePixmapFlags) &&
         CHECK_PLANEMASK(pGC, infoRec->WritePixmapFlags) &&
         CHECK_NO_GXCOPY(pGC, infoRec->WritePixmapFlags)) ||
        ((format == XYBitmap) && !depthBug && infoRec->WriteBitmap &&
         CHECK_ROP(pGC, infoRec->WriteBitmapFlags) &&
         CHECK_ROPSRC(pGC, infoRec->WriteBitmapFlags) &&
         CHECK_PLANEMASK(pGC, infoRec->WriteBitmapFlags) &&
         CHECK_COLORS(pGC, infoRec->WriteBitmapFlags) &&
         !(infoRec->WriteBitmapFlags & TRANSPARENCY_ONLY)) ||
        ((format == XYPixmap) && !depthBug && infoRec->WriteBitmap &&
         CHECK_ROP(pGC, infoRec->WriteBitmapFlags) &&
         CHECK_ROPSRC(pGC, infoRec->WriteBitmapFlags) &&
         !(infoRec->WriteBitmapFlags & NO_PLANEMASK) &&
         !(infoRec->WriteBitmapFlags & TRANSPARENCY_ONLY))) {

        int MaxBoxes = RegionNumRects(pGC->pCompositeClip);
        BoxPtr pbox, pClipBoxes;
        int nboxes, srcx, srcy, srcwidth;
        xRectangle TheRect;

        TheRect.x = pDraw->x + x;
        TheRect.y = pDraw->y + y;
        TheRect.width = w;
        TheRect.height = h;

        if (MaxBoxes > (infoRec->PreAllocSize / sizeof(BoxRec))) {
            pClipBoxes = malloc(MaxBoxes * sizeof(BoxRec));
            if (!pClipBoxes)
                return;
        }
        else
            pClipBoxes = (BoxPtr) infoRec->PreAllocMem;

        nboxes = XAAGetRectClipBoxes(pGC, pClipBoxes, 1, &TheRect);
        pbox = pClipBoxes;

        if (format == XYBitmap) {
            srcwidth = BitmapBytePad(leftPad + w);
            while (nboxes--) {
                srcx = pbox->x1 - TheRect.x + leftPad;
                srcy = pbox->y1 - TheRect.y;
                (*infoRec->WriteBitmap) (infoRec->pScrn, pbox->x1, pbox->y1,
                                         pbox->x2 - pbox->x1,
                                         pbox->y2 - pbox->y1,
                                         (unsigned char *) pImage +
                                         (srcwidth * srcy) + ((srcx >> 5) << 2),
                                         srcwidth, srcx & 31, pGC->fgPixel,
                                         pGC->bgPixel, pGC->alu,
                                         pGC->planemask);
                pbox++;
            }
        }
        else if (format == ZPixmap) {
            int Bpp = bpp >> 3;

            srcwidth = PixmapBytePad(leftPad + w, depth);
            while (nboxes--) {
                srcx = pbox->x1 - TheRect.x + leftPad;
                srcy = pbox->y1 - TheRect.y;
                (*infoRec->WritePixmap) (infoRec->pScrn, pbox->x1, pbox->y1,
                                         pbox->x2 - pbox->x1,
                                         pbox->y2 - pbox->y1,
                                         (unsigned char *) pImage +
                                         (srcwidth * srcy) + (srcx * Bpp),
                                         srcwidth, pGC->alu, pGC->planemask, -1,
                                         Bpp << 3, depth);
                pbox++;
            }
        }
        else {                  /* XYPixmap */
            int depth = pGC->depth;
            int numBox, increment;
            unsigned long i, mask;
            BoxPtr pntBox;

            srcwidth = BitmapBytePad(w + leftPad);
            increment = h * srcwidth;
            i = 1 << (depth - 1);
            mask = ~0;

            if ((infoRec->pScrn->overlayFlags & OVERLAY_8_32_PLANAR) &&
                (pGC->depth == 8)) {
                i = 0x80000000;
                mask = 0xff000000;
            }

            for (; i & mask; i >>= 1, pImage += increment) {
                if (i & pGC->planemask) {
                    pntBox = pbox;
                    numBox = nboxes;
                    while (numBox--) {
                        srcx = pntBox->x1 - TheRect.x + leftPad;
                        srcy = pntBox->y1 - TheRect.y;
                        (*infoRec->WriteBitmap) (infoRec->pScrn,
                                                 pntBox->x1, pntBox->y1,
                                                 pntBox->x2 - pntBox->x1,
                                                 pntBox->y2 - pntBox->y1,
                                                 (unsigned char *) pImage +
                                                 (srcwidth * srcy) +
                                                 ((srcx >> 5) << 2), srcwidth,
                                                 srcx & 31, ~0, 0, pGC->alu, i);
                        pntBox++;
                    }
                }
            }

        }

        if (pClipBoxes != (BoxPtr) infoRec->PreAllocMem)
            free(pClipBoxes);
    }
    else
        XAAFallbackOps.PutImage(pDraw, pGC, depth, x, y, w, h, leftPad,
                                format, pImage);
}
