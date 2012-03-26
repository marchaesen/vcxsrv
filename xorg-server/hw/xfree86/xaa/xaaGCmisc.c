
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include <X11/fonts/fontstruct.h>
#include "dixfontstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "migc.h"
#include "mi.h"
#include "gcstruct.h"
#include "pixmapstr.h"

void
XAAValidateCopyArea(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (infoRec->CopyArea &&
        CHECK_PLANEMASK(pGC, infoRec->CopyAreaFlags) &&
        CHECK_ROP(pGC, infoRec->CopyAreaFlags) &&
        CHECK_ROPSRC(pGC, infoRec->CopyAreaFlags)
        )
        pGC->ops->CopyArea = infoRec->CopyArea;
    else
        pGC->ops->CopyArea = XAAFallbackOps.CopyArea;
}

void
XAAValidatePutImage(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (infoRec->PutImage &&
        CHECK_PLANEMASK(pGC, infoRec->PutImageFlags) &&
        CHECK_ROP(pGC, infoRec->PutImageFlags) &&
        CHECK_ROPSRC(pGC, infoRec->PutImageFlags) &&
        CHECK_COLORS(pGC, infoRec->PutImageFlags)
        )
        pGC->ops->PutImage = infoRec->PutImage;
    else
        pGC->ops->PutImage = XAAFallbackOps.PutImage;
}

void
XAAValidateCopyPlane(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (infoRec->CopyPlane &&
        CHECK_PLANEMASK(pGC, infoRec->CopyPlaneFlags) &&
        CHECK_ROP(pGC, infoRec->CopyPlaneFlags) &&
        CHECK_ROPSRC(pGC, infoRec->CopyPlaneFlags) &&
        CHECK_COLORS(pGC, infoRec->CopyPlaneFlags)
        )
        pGC->ops->CopyPlane = infoRec->CopyPlane;
    else
        pGC->ops->CopyPlane = XAAFallbackOps.CopyPlane;
}

void
XAAValidatePushPixels(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (infoRec->PushPixelsSolid &&
        (pGC->fillStyle == FillSolid) &&
        CHECK_PLANEMASK(pGC, infoRec->PushPixelsFlags) &&
        CHECK_ROP(pGC, infoRec->PushPixelsFlags) &&
        CHECK_ROPSRC(pGC, infoRec->PushPixelsFlags) &&
        CHECK_FG(pGC, infoRec->PushPixelsFlags) &&
        (!(infoRec->PushPixelsFlags & TRANSPARENCY_GXCOPY_ONLY) ||
         (pGC->alu == GXcopy))
        )
        pGC->ops->PushPixels = infoRec->PushPixelsSolid;
    else
        pGC->ops->PushPixels = XAAFallbackOps.PushPixels;

}

/* We make the assumption that the FillSpans, PolyFillRect, FillPolygon
   and PolyFillArc functions are linked in a way that they all have 
   the same rop/color/planemask restrictions. If the driver provides 
   a GC level replacement for these, it will need to supply a new 
   Validate functions if it breaks this assumption */

void
XAAValidateFillSpans(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (pGC->fillStyle != FillTiled)
        changes &= ~GCTile;
    if ((pGC->fillStyle == FillTiled) || (pGC->fillStyle == FillSolid))
        changes &= ~GCStipple;
    if (!changes)
        return;

    pGC->ops->FillSpans = XAAFallbackOps.FillSpans;
    pGC->ops->PolyFillRect = XAAFallbackOps.PolyFillRect;
    pGC->ops->FillPolygon = XAAFallbackOps.FillPolygon;
    pGC->ops->PolyFillArc = XAAFallbackOps.PolyFillArc;

    switch (pGC->fillStyle) {
    case FillSolid:
        if (infoRec->FillSpansSolid &&
            CHECK_PLANEMASK(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_ROP(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_FG(pGC, infoRec->FillSpansSolidFlags)
            ) {
            pGC->ops->FillSpans = infoRec->FillSpansSolid;
            pGC->ops->PolyFillRect = infoRec->PolyFillRectSolid;
            pGC->ops->FillPolygon = infoRec->FillPolygonSolid;
            pGC->ops->PolyFillArc = infoRec->PolyFillArcSolid;
        }
        break;
        /* The [Stippled/OpaqueStippled/Tiled]FillChooser 
           functions do the validating */
    case FillStippled:
        if (infoRec->FillSpansStippled) {
            pGC->ops->FillSpans = infoRec->FillSpansStippled;
            pGC->ops->PolyFillRect = infoRec->PolyFillRectStippled;
            if (infoRec->FillPolygonStippled)
                pGC->ops->FillPolygon = infoRec->FillPolygonStippled;
            else
                pGC->ops->FillPolygon = miFillPolygon;
            pGC->ops->PolyFillArc = miPolyFillArc;
        }
        break;
    case FillOpaqueStippled:
        if (infoRec->FillSpansOpaqueStippled) {
            pGC->ops->FillSpans = infoRec->FillSpansOpaqueStippled;
            pGC->ops->PolyFillRect = infoRec->PolyFillRectOpaqueStippled;
            if (infoRec->FillPolygonOpaqueStippled)
                pGC->ops->FillPolygon = infoRec->FillPolygonOpaqueStippled;
            else
                pGC->ops->FillPolygon = miFillPolygon;
            pGC->ops->PolyFillArc = miPolyFillArc;
        }
        break;
    case FillTiled:
        if (infoRec->FillSpansTiled) {
            pGC->ops->FillSpans = infoRec->FillSpansTiled;
            pGC->ops->PolyFillRect = infoRec->PolyFillRectTiled;
            if (infoRec->FillPolygonTiled)
                pGC->ops->FillPolygon = infoRec->FillPolygonTiled;
            else
                pGC->ops->FillPolygon = miFillPolygon;
            pGC->ops->PolyFillArc = miPolyFillArc;
        }
        break;
    default:
        return;
    }
}

/* We make the assumption that these Text8/16 and GlyphBlt functions
   are linked in a way that they all have the same rop/color/planemask
   restrictions. If the driver provides a GC level replacement for
   these, it will need to supply a new Validate functions if it breaks
   this assumption */

void
XAAValidatePolyGlyphBlt(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    Bool BigFont = FALSE;

    pGC->ops->PolyText8 = XAAFallbackOps.PolyText8;
    pGC->ops->PolyText16 = XAAFallbackOps.PolyText16;
    pGC->ops->PolyGlyphBlt = XAAFallbackOps.PolyGlyphBlt;

    if (!pGC->font)
        return;
    if (pGC->fillStyle != FillSolid)
        return;

    if ((FONTMAXBOUNDS(pGC->font, rightSideBearing) -
         FONTMINBOUNDS(pGC->font, leftSideBearing) > 32))
        BigFont = TRUE;

    /* no funny business */
    if ((FONTMINBOUNDS(pGC->font, characterWidth) <= 0) ||
        ((FONTASCENT(pGC->font) + FONTDESCENT(pGC->font)) <= 0))
        return;

    /* Check for TE Fonts */
    if (!TERMINALFONT(pGC->font) || BigFont) {
        if (infoRec->PolyGlyphBltNonTE &&
            CHECK_PLANEMASK(pGC, infoRec->PolyGlyphBltNonTEFlags) &&
            CHECK_ROP(pGC, infoRec->PolyGlyphBltNonTEFlags) &&
            CHECK_ROPSRC(pGC, infoRec->PolyGlyphBltNonTEFlags) &&
            CHECK_FG(pGC, infoRec->PolyGlyphBltNonTEFlags) &&
            (!(infoRec->PolyGlyphBltNonTEFlags & TRANSPARENCY_GXCOPY_ONLY) ||
             (pGC->alu == GXcopy))
            ) {
            pGC->ops->PolyText8 = infoRec->PolyText8NonTE;
            pGC->ops->PolyText16 = infoRec->PolyText16NonTE;
            pGC->ops->PolyGlyphBlt = infoRec->PolyGlyphBltNonTE;
        }
    }
    else {
        if (infoRec->PolyGlyphBltTE &&
            CHECK_PLANEMASK(pGC, infoRec->PolyGlyphBltTEFlags) &&
            CHECK_ROP(pGC, infoRec->PolyGlyphBltTEFlags) &&
            CHECK_ROPSRC(pGC, infoRec->PolyGlyphBltNonTEFlags) &&
            CHECK_FG(pGC, infoRec->PolyGlyphBltTEFlags) &&
            (!(infoRec->PolyGlyphBltTEFlags & TRANSPARENCY_GXCOPY_ONLY) ||
             (pGC->alu == GXcopy))
            ) {
            pGC->ops->PolyText8 = infoRec->PolyText8TE;
            pGC->ops->PolyText16 = infoRec->PolyText16TE;
            pGC->ops->PolyGlyphBlt = infoRec->PolyGlyphBltTE;
        }
    }
}

void
XAAValidateImageGlyphBlt(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    Bool BigFont = FALSE;

    pGC->ops->ImageText8 = XAAFallbackOps.ImageText8;
    pGC->ops->ImageText16 = XAAFallbackOps.ImageText16;
    pGC->ops->ImageGlyphBlt = XAAFallbackOps.ImageGlyphBlt;

    if (!pGC->font)
        return;

    if ((FONTMAXBOUNDS(pGC->font, rightSideBearing) -
         FONTMINBOUNDS(pGC->font, leftSideBearing) > 32))
        BigFont = TRUE;

    /* no funny business */
    if ((FONTMINBOUNDS(pGC->font, characterWidth) <= 0) ||
        ((FONTASCENT(pGC->font) + FONTDESCENT(pGC->font)) <= 0))
        return;

    /* Check for TE Fonts */
    if (!TERMINALFONT(pGC->font) || BigFont || (pGC->depth == 32)) {
        if (infoRec->ImageGlyphBltNonTE &&
            CHECK_PLANEMASK(pGC, infoRec->ImageGlyphBltNonTEFlags) &&
            CHECK_FG(pGC, infoRec->ImageGlyphBltNonTEFlags) &&
            infoRec->SetupForSolidFill &&
            CHECK_PLANEMASK(pGC, infoRec->SolidFillFlags) &&
            CHECK_BG(pGC, infoRec->SolidFillFlags)) {
            pGC->ops->ImageText8 = infoRec->ImageText8NonTE;
            pGC->ops->ImageText16 = infoRec->ImageText16NonTE;
            pGC->ops->ImageGlyphBlt = infoRec->ImageGlyphBltNonTE;
        }
    }
    else if (infoRec->ImageGlyphBltTE &&
             CHECK_PLANEMASK(pGC, infoRec->ImageGlyphBltTEFlags)) {
        if (!(infoRec->ImageGlyphBltTEFlags & TRANSPARENCY_ONLY) &&
            CHECK_COLORS(pGC, infoRec->ImageGlyphBltTEFlags)) {
            pGC->ops->ImageText8 = infoRec->ImageText8TE;
            pGC->ops->ImageText16 = infoRec->ImageText16TE;
            pGC->ops->ImageGlyphBlt = infoRec->ImageGlyphBltTE;
        }
        else {
            if (CHECK_FG(pGC, infoRec->ImageGlyphBltTEFlags) &&
                infoRec->SetupForSolidFill &&
                CHECK_PLANEMASK(pGC, infoRec->SolidFillFlags) &&
                CHECK_BG(pGC, infoRec->SolidFillFlags)) {
                pGC->ops->ImageText8 = infoRec->ImageText8TE;
                pGC->ops->ImageText16 = infoRec->ImageText16TE;
                pGC->ops->ImageGlyphBlt = infoRec->ImageGlyphBltTE;
            }
        }
    }
}

void
XAAValidatePolylines(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAAGCPtr pGCPriv = (XAAGCPtr) dixLookupPrivate(&pGC->devPrivates,
                                                   XAAGetGCKey());

    if (pGC->lineStyle == LineSolid)
        changes &= ~GCDashList;
    if (!changes)
        return;

    pGC->ops->PolySegment = XAAFallbackOps.PolySegment;
    pGC->ops->Polylines = XAAFallbackOps.Polylines;
    pGC->ops->PolyRectangle = XAAFallbackOps.PolyRectangle;
    pGC->ops->PolyArc = XAAFallbackOps.PolyArc;

    if ((pGC->ops->FillSpans != XAAFallbackOps.FillSpans) &&
        (pGC->lineWidth > 0)) {

        pGC->ops->PolyArc = miPolyArc;
        pGC->ops->PolySegment = miPolySegment;
        pGC->ops->PolyRectangle = miPolyRectangle;
        if (pGC->lineStyle == LineSolid)
            pGC->ops->Polylines = miWideLine;
        else
            pGC->ops->Polylines = miWideDash;
    }

    if ((pGC->lineWidth == 0) && (pGC->fillStyle == FillSolid)) {

        if (pGC->lineStyle == LineSolid) {

            if (infoRec->PolyRectangleThinSolid &&
                CHECK_PLANEMASK(pGC, infoRec->PolyRectangleThinSolidFlags) &&
                CHECK_ROP(pGC, infoRec->PolyRectangleThinSolidFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolyRectangleThinSolidFlags) &&
                CHECK_FG(pGC, infoRec->PolyRectangleThinSolidFlags)) {

                pGC->ops->PolyRectangle = infoRec->PolyRectangleThinSolid;
            }

            if (infoRec->PolySegmentThinSolid &&
                CHECK_PLANEMASK(pGC, infoRec->PolySegmentThinSolidFlags) &&
                CHECK_ROP(pGC, infoRec->PolySegmentThinSolidFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolySegmentThinSolidFlags) &&
                CHECK_FG(pGC, infoRec->PolySegmentThinSolidFlags)) {

                pGC->ops->PolySegment = infoRec->PolySegmentThinSolid;
            }

            if (infoRec->PolylinesThinSolid &&
                CHECK_PLANEMASK(pGC, infoRec->PolylinesThinSolidFlags) &&
                CHECK_ROP(pGC, infoRec->PolylinesThinSolidFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolylinesThinSolidFlags) &&
                CHECK_FG(pGC, infoRec->PolylinesThinSolidFlags)) {

                pGC->ops->Polylines = infoRec->PolylinesThinSolid;
            }
        }
        else if ((pGC->lineStyle == LineOnOffDash) && pGCPriv->DashPattern) {

            if (infoRec->PolySegmentThinDashed &&
                !(infoRec->PolySegmentThinDashedFlags & NO_TRANSPARENCY) &&
                ((pGC->alu == GXcopy) || !(infoRec->PolySegmentThinDashedFlags &
                                           TRANSPARENCY_GXCOPY_ONLY)) &&
                CHECK_PLANEMASK(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_ROP(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_FG(pGC, infoRec->PolySegmentThinDashedFlags)) {

                pGC->ops->PolySegment = infoRec->PolySegmentThinDashed;
            }

            if (infoRec->PolylinesThinDashed &&
                !(infoRec->PolylinesThinDashedFlags & NO_TRANSPARENCY) &&
                ((pGC->alu == GXcopy) || !(infoRec->PolylinesThinDashedFlags &
                                           TRANSPARENCY_GXCOPY_ONLY)) &&
                CHECK_PLANEMASK(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_ROP(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_FG(pGC, infoRec->PolylinesThinDashedFlags)) {

                pGC->ops->Polylines = infoRec->PolylinesThinDashed;
            }

            if (pGC->ops->Polylines != XAAFallbackOps.Polylines)
                pGC->ops->PolyRectangle = miPolyRectangle;

        }
        else if (pGCPriv->DashPattern && (pGC->depth != 32)) {
            /* LineDoubleDash */
            if (infoRec->PolySegmentThinDashed &&
                !(infoRec->PolySegmentThinDashedFlags & TRANSPARENCY_ONLY) &&
                CHECK_PLANEMASK(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_ROP(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolySegmentThinDashedFlags) &&
                CHECK_COLORS(pGC, infoRec->PolySegmentThinDashedFlags)) {

                pGC->ops->PolySegment = infoRec->PolySegmentThinDashed;
            }

            if (infoRec->PolylinesThinDashed &&
                !(infoRec->PolylinesThinDashedFlags & TRANSPARENCY_ONLY) &&
                CHECK_PLANEMASK(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_ROP(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_ROPSRC(pGC, infoRec->PolylinesThinDashedFlags) &&
                CHECK_COLORS(pGC, infoRec->PolylinesThinDashedFlags)) {

                pGC->ops->Polylines = infoRec->PolylinesThinDashed;
            }

            if (pGC->ops->Polylines != XAAFallbackOps.Polylines)
                pGC->ops->PolyRectangle = miPolyRectangle;

        }
    }

    if (infoRec->PolylinesWideSolid &&
        (pGC->lineWidth > 0) &&
        (pGC->fillStyle == FillSolid) &&
        (pGC->lineStyle == LineSolid) &&
        CHECK_PLANEMASK(pGC, infoRec->PolylinesWideSolidFlags) &&
        CHECK_ROP(pGC, infoRec->PolylinesWideSolidFlags) &&
        CHECK_ROPSRC(pGC, infoRec->PolylinesWideSolidFlags) &&
        CHECK_FG(pGC, infoRec->PolylinesWideSolidFlags)) {

        pGC->ops->Polylines = infoRec->PolylinesWideSolid;
    }
}
