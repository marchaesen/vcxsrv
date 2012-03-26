#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xf86fbman.h"
#include "servermd.h"

/*
 * XAA Config options
 */

typedef enum {
    XAAOPT_SCREEN_TO_SCREEN_COPY,
    XAAOPT_SOLID_FILL_RECT,
    XAAOPT_SOLID_FILL_TRAP,
    XAAOPT_SOLID_TWO_POINT_LINE,
    XAAOPT_SOLID_BRESENHAM_LINE,
    XAAOPT_SOLID_HORVERT_LINE,
    XAAOPT_DASHED_TWO_POINT_LINE,
    XAAOPT_DASHED_BRESENHAM_LINE,
    XAAOPT_MONO_8x8_PATTERN_FILL_RECT,
    XAAOPT_MONO_8x8_PATTERN_FILL_TRAP,
    XAAOPT_COL_8x8_PATTERN_FILL_RECT,
    XAAOPT_COL_8x8_PATTERN_FILL_TRAP,
    XAAOPT_CPU_TO_SCREEN_COL_EXP_FILL,
    XAAOPT_SCANLINE_CPU_TO_SCREEN_COL_EXP_FILL,
    XAAOPT_SCREEN_TO_SCREEN_COL_EXP_FILL,
    XAAOPT_IMAGE_WRITE_RECT,
    XAAOPT_SCANLINE_IMAGE_WRITE_RECT,
    XAAOPT_WRITE_BITMAP,
    XAAOPT_WRITE_PIXMAP,
    XAAOPT_PIXMAP_CACHE,
    XAAOPT_OFFSCREEN_PIXMAPS,
    XAAOPT_HAS_DUMB_INVERTED_OPTION_SENSE
} XAAOpts;

static const OptionInfoRec XAAOptions[] = {
    {XAAOPT_SCREEN_TO_SCREEN_COPY, "XaaNoScreenToScreenCopy",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SOLID_FILL_RECT, "XaaNoSolidFillRect",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SOLID_FILL_TRAP, "XaaNoSolidFillTrap",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SOLID_TWO_POINT_LINE, "XaaNoSolidTwoPointLine",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SOLID_BRESENHAM_LINE, "XaaNoSolidBresenhamLine",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SOLID_HORVERT_LINE, "XaaNoSolidHorVertLine",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_DASHED_TWO_POINT_LINE, "XaaNoDashedTwoPointLine",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_DASHED_BRESENHAM_LINE, "XaaNoDashedBresenhamLine",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_MONO_8x8_PATTERN_FILL_RECT, "XaaNoMono8x8PatternFillRect",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_MONO_8x8_PATTERN_FILL_TRAP, "XaaNoMono8x8PatternFillTrap",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_COL_8x8_PATTERN_FILL_RECT, "XaaNoColor8x8PatternFillRect",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_COL_8x8_PATTERN_FILL_TRAP, "XaaNoColor8x8PatternFillTrap",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_CPU_TO_SCREEN_COL_EXP_FILL, "XaaNoCPUToScreenColorExpandFill",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SCANLINE_CPU_TO_SCREEN_COL_EXP_FILL,
     "XaaNoScanlineCPUToScreenColorExpandFill",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SCREEN_TO_SCREEN_COL_EXP_FILL, "XaaNoScreenToScreenColorExpandFill",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_IMAGE_WRITE_RECT, "XaaNoImageWriteRect",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_SCANLINE_IMAGE_WRITE_RECT, "XaaNoScanlineImageWriteRect",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_WRITE_BITMAP, "XaaNoWriteBitmap",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_WRITE_PIXMAP, "XaaNoWritePixmap",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_PIXMAP_CACHE, "XaaNoPixmapCache",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_OFFSCREEN_PIXMAPS, "XaaNoOffscreenPixmaps",
     OPTV_BOOLEAN, {0}, FALSE},
    {XAAOPT_HAS_DUMB_INVERTED_OPTION_SENSE, "XaaOffscreenPixmaps",
     OPTV_BOOLEAN, {0}, FALSE},
    {-1, NULL,
     OPTV_NONE, {0}, FALSE}
};

static XF86ModuleVersionInfo xaaVersRec = {
    "xaa",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    XAA_VERSION_MAJOR,
    XAA_VERSION_MINOR,
    XAA_VERSION_RELEASE,
    ABI_CLASS_VIDEODRV,         /* requires the video driver ABI */
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_NONE,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData xaaModuleData = { &xaaVersRec, NULL, NULL };

Bool
XAAInitAccel(ScreenPtr pScreen, XAAInfoRecPtr infoRec)
{
    int index = pScreen->myNum;
    ScrnInfoPtr pScrn = xf86Screens[index];
    Bool HaveScreenToScreenCopy = FALSE;
    Bool HaveColorExpansion = FALSE;
    Bool HaveScanlineColorExpansion = FALSE;
    Bool HaveSolidFillRect = FALSE;
    Bool HaveMono8x8PatternFillRect = FALSE;
    Bool HaveColor8x8PatternFillRect = FALSE;
    Bool HaveSolidFillTrap = FALSE;
    Bool HaveMono8x8PatternFillTrap = FALSE;
    Bool HaveColor8x8PatternFillTrap = FALSE;
    Bool HaveSolidTwoPointLine = FALSE;
    Bool HaveSolidBresenhamLine = FALSE;
    Bool HaveSolidHorVertLine = FALSE;
    Bool HaveDashedTwoPointLine = FALSE;
    Bool HaveDashedBresenhamLine = FALSE;
    Bool HaveImageWriteRect = FALSE;
    Bool HaveScanlineImageWriteRect = FALSE;
    Bool HaveScreenToScreenColorExpandFill = FALSE;
    OptionInfoPtr options;
    int is_shared = 0;
    int i;

    options = xnfalloc(sizeof(XAAOptions));
    (void) memcpy(options, XAAOptions, sizeof(XAAOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, options);

    infoRec->pScrn = pScrn;
    infoRec->NeedToSync = FALSE;

    /* must have a Sync function */
    if (!infoRec->Sync)
        return FALSE;
    for (i = 0; i < pScrn->numEntities; i++) {
        if (xf86IsEntityShared(pScrn->entityList[i]))
            is_shared = 1;
    }

    /* If this PCI entity has IS_SHARED_ACCEL set in entityProp
     * then a RestoreAccelState function is required
     */
    if (!infoRec->RestoreAccelState && is_shared)
        return FALSE;

    if (infoRec->RestoreAccelState) {
        if (!XAAInitStateWrap(pScreen, infoRec))
            return FALSE;
    }

    if (serverGeneration == 1)
        xf86DrvMsg(index, X_INFO,
                   "Using XFree86 Acceleration Architecture (XAA)\n");

    /************** Low Level *************/

    if (!infoRec->SetClippingRectangle || !infoRec->DisableClipping) {
        infoRec->ClippingFlags = 0;
        infoRec->SetClippingRectangle = NULL;
        infoRec->DisableClipping = NULL;
    }

    /**** CopyArea ****/

    if (infoRec->SetupForScreenToScreenCopy &&
        infoRec->SubsequentScreenToScreenCopy &&
        !xf86ReturnOptValBool(options, XAAOPT_SCREEN_TO_SCREEN_COPY, FALSE)) {
        HaveScreenToScreenCopy = TRUE;
    }
    else {
        infoRec->ScreenToScreenCopyFlags = 0;
        infoRec->SetupForScreenToScreenCopy = NULL;
        infoRec->SubsequentScreenToScreenCopy = NULL;
    }

    /**** Solid Filled Rects ****/

    if (infoRec->SetupForSolidFill && infoRec->SubsequentSolidFillRect &&
        !xf86ReturnOptValBool(options, XAAOPT_SOLID_FILL_RECT, FALSE)) {
        HaveSolidFillRect = TRUE;
        if (infoRec->SubsequentSolidFillTrap &&
            !xf86ReturnOptValBool(options, XAAOPT_SOLID_FILL_TRAP, FALSE))
            HaveSolidFillTrap = TRUE;
        else
            infoRec->SubsequentSolidFillTrap = NULL;
    }
    else {
        infoRec->SolidFillFlags = 0;
        infoRec->SetupForSolidFill = NULL;
        infoRec->SubsequentSolidFillRect = NULL;
        infoRec->SubsequentSolidFillTrap = NULL;
    }

    /**** Solid lines ****/

    if (infoRec->SetupForSolidLine) {
        if (infoRec->SubsequentSolidTwoPointLine &&
            !xf86ReturnOptValBool(options, XAAOPT_SOLID_TWO_POINT_LINE, FALSE))
            HaveSolidTwoPointLine = TRUE;
        if (infoRec->SubsequentSolidBresenhamLine &&
            !xf86ReturnOptValBool(options, XAAOPT_SOLID_BRESENHAM_LINE,
                                  FALSE)) {
            HaveSolidBresenhamLine = TRUE;

            if (infoRec->SolidBresenhamLineErrorTermBits)
                infoRec->SolidBresenhamLineErrorTermBits =
                    ~((1 << infoRec->SolidBresenhamLineErrorTermBits) - 1);
        }

        if (infoRec->SubsequentSolidHorVertLine &&
            !xf86ReturnOptValBool(options, XAAOPT_SOLID_HORVERT_LINE, FALSE))
            HaveSolidHorVertLine = TRUE;
        else if (HaveSolidTwoPointLine) {
            infoRec->SubsequentSolidHorVertLine = XAASolidHorVertLineAsTwoPoint;
            HaveSolidHorVertLine = TRUE;
        }
        else if (HaveSolidBresenhamLine) {
            infoRec->SubsequentSolidHorVertLine =
                XAASolidHorVertLineAsBresenham;
            HaveSolidHorVertLine = TRUE;
        }
    }

    /* XXX Should this also check for XAAOPT_SOLID_HORVERT_LINE? */
    if (!HaveSolidTwoPointLine &&
        !HaveSolidBresenhamLine && !HaveSolidHorVertLine && HaveSolidFillRect) {
        infoRec->SetupForSolidLine = infoRec->SetupForSolidFill;
        infoRec->SubsequentSolidHorVertLine = XAASolidHorVertLineAsRects;
        infoRec->SolidLineFlags = infoRec->SolidFillFlags;
        HaveSolidHorVertLine = TRUE;
    }

    if (!HaveSolidTwoPointLine)
        infoRec->SubsequentSolidTwoPointLine = NULL;
    if (!HaveSolidBresenhamLine)
        infoRec->SubsequentSolidBresenhamLine = NULL;
    if (!HaveSolidHorVertLine)
        infoRec->SubsequentSolidHorVertLine = NULL;

    /* Disable all if nothing left over */
    if (!HaveSolidTwoPointLine &&
        !HaveSolidBresenhamLine && !HaveSolidHorVertLine) {
        infoRec->SolidLineFlags = 0;
        infoRec->SetupForSolidLine = NULL;
    }

    /**** 8x8 Mono Pattern Filled Rects ****/

    if (infoRec->SetupForMono8x8PatternFill &&
        infoRec->SubsequentMono8x8PatternFillRect &&
        !xf86ReturnOptValBool(options,
                              XAAOPT_MONO_8x8_PATTERN_FILL_RECT, FALSE)) {
        HaveMono8x8PatternFillRect = TRUE;
        if (infoRec->SubsequentMono8x8PatternFillTrap &&
            !xf86ReturnOptValBool(options,
                                  XAAOPT_MONO_8x8_PATTERN_FILL_TRAP, FALSE))
            HaveMono8x8PatternFillTrap = TRUE;

        if (infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_BITS) {
            infoRec->CanDoMono8x8 = TRUE;
        }
        else {                  /* others require caching */
            int min_pitch;

            infoRec->PixmapCacheFlags |= CACHE_MONO_8x8;

            switch (pScrn->bitsPerPixel) {
            case 32:
                min_pitch = 2;
                break;
            case 24:
                min_pitch = 3;
                break;
            case 16:
                min_pitch = 4;
                break;
            default:
                min_pitch = 8;
                break;
            }

            if (min_pitch > infoRec->MonoPatternPitch)
                infoRec->MonoPatternPitch = min_pitch;

            if (infoRec->Mono8x8PatternFillFlags &
                HARDWARE_PATTERN_PROGRAMMED_ORIGIN) {
                if (!infoRec->CacheWidthMono8x8Pattern ||
                    !infoRec->CacheHeightMono8x8Pattern) {
                    infoRec->CacheWidthMono8x8Pattern =
                        infoRec->MonoPatternPitch;
                    infoRec->CacheHeightMono8x8Pattern = 1;
                }
            }
            else {
                int numPerLine = 128 / infoRec->MonoPatternPitch;

                if (!infoRec->CacheWidthMono8x8Pattern ||
                    !infoRec->CacheHeightMono8x8Pattern) {
                    infoRec->CacheWidthMono8x8Pattern =
                        numPerLine * infoRec->MonoPatternPitch;
                    infoRec->CacheHeightMono8x8Pattern =
                        (64 + numPerLine - 1) / numPerLine;
                }
            }
        }
    }
    else {
        infoRec->Mono8x8PatternFillFlags = 0;
        infoRec->SetupForMono8x8PatternFill = NULL;
        infoRec->SubsequentMono8x8PatternFillRect = NULL;
    }

    /**** Dashed lines ****/

    if (infoRec->SetupForDashedLine && infoRec->DashPatternMaxLength) {
        if (infoRec->SubsequentDashedTwoPointLine &&
            !xf86ReturnOptValBool(options, XAAOPT_DASHED_TWO_POINT_LINE, FALSE))
            HaveDashedTwoPointLine = TRUE;
        if (infoRec->SubsequentDashedBresenhamLine &&
            !xf86ReturnOptValBool(options, XAAOPT_DASHED_BRESENHAM_LINE,
                                  FALSE)) {
            HaveDashedBresenhamLine = TRUE;

            if (infoRec->DashedBresenhamLineErrorTermBits)
                infoRec->DashedBresenhamLineErrorTermBits =
                    ~((1 << infoRec->DashedBresenhamLineErrorTermBits) - 1);
        }
    }

    if (!HaveDashedTwoPointLine)
        infoRec->SubsequentDashedTwoPointLine = NULL;
    if (!HaveDashedBresenhamLine)
        infoRec->SubsequentDashedBresenhamLine = NULL;

    /* Disable all if nothing left over */
    if (!HaveDashedTwoPointLine && !HaveDashedBresenhamLine) {
        infoRec->DashedLineFlags = 0;
        infoRec->SetupForDashedLine = NULL;
    }

    /**** 8x8 Color Pattern Filled Rects ****/

    if (infoRec->SetupForColor8x8PatternFill &&
        infoRec->SubsequentColor8x8PatternFillRect &&
        !xf86ReturnOptValBool(options, XAAOPT_COL_8x8_PATTERN_FILL_RECT,
                              FALSE)) {
        HaveColor8x8PatternFillRect = TRUE;
        if (infoRec->SubsequentColor8x8PatternFillTrap &&
            !xf86ReturnOptValBool(options, XAAOPT_COL_8x8_PATTERN_FILL_TRAP,
                                  FALSE))
            HaveColor8x8PatternFillTrap = TRUE;
        else
            infoRec->SubsequentColor8x8PatternFillTrap = NULL;

        infoRec->PixmapCacheFlags |= CACHE_COLOR_8x8;

        if (infoRec->Color8x8PatternFillFlags &
            HARDWARE_PATTERN_PROGRAMMED_ORIGIN) {
            if (!infoRec->CacheWidthColor8x8Pattern ||
                !infoRec->CacheHeightColor8x8Pattern) {
                infoRec->CacheWidthColor8x8Pattern = 64;
                infoRec->CacheHeightColor8x8Pattern = 1;
            }
        }
        else {
            if (!infoRec->CacheWidthColor8x8Pattern ||
                !infoRec->CacheHeightColor8x8Pattern) {
                infoRec->CacheWidthColor8x8Pattern = 128;
                infoRec->CacheHeightColor8x8Pattern = 8;
            }
        }
    }
    else {
        infoRec->Color8x8PatternFillFlags = 0;
        infoRec->SetupForColor8x8PatternFill = NULL;
        infoRec->SubsequentColor8x8PatternFillRect = NULL;
        infoRec->SubsequentColor8x8PatternFillTrap = NULL;
    }

    /**** Color Expansion ****/

    if (infoRec->SetupForCPUToScreenColorExpandFill &&
        infoRec->ColorExpandBase &&
        infoRec->SubsequentCPUToScreenColorExpandFill &&
        !xf86ReturnOptValBool(options, XAAOPT_CPU_TO_SCREEN_COL_EXP_FILL,
                              FALSE)) {
        int dwordsNeeded = pScrn->virtualX;

        infoRec->ColorExpandRange >>= 2;        /* convert to DWORDS */
        HaveColorExpansion = TRUE;

        if (infoRec->CPUToScreenColorExpandFillFlags &
            LEFT_EDGE_CLIPPING_NEGATIVE_X)
            dwordsNeeded += 31;
        dwordsNeeded = (dwordsNeeded + 31) >> 5;
        if (dwordsNeeded > infoRec->ColorExpandRange)
            infoRec->CPUToScreenColorExpandFillFlags |= CPU_TRANSFER_BASE_FIXED;
    }
    else {
        infoRec->CPUToScreenColorExpandFillFlags = 0;
        infoRec->SetupForCPUToScreenColorExpandFill = NULL;
        infoRec->SubsequentCPUToScreenColorExpandFill = NULL;
    }

    /**** Scanline Color Expansion ****/

    if (infoRec->SetupForScanlineCPUToScreenColorExpandFill &&
        infoRec->SubsequentScanlineCPUToScreenColorExpandFill &&
        infoRec->SubsequentColorExpandScanline &&
        infoRec->ScanlineColorExpandBuffers &&
        (infoRec->NumScanlineColorExpandBuffers > 0) &&
        !xf86ReturnOptValBool(options,
                              XAAOPT_SCANLINE_CPU_TO_SCREEN_COL_EXP_FILL,
                              FALSE)) {
        HaveScanlineColorExpansion = TRUE;
    }
    else {
        infoRec->ScanlineCPUToScreenColorExpandFillFlags = 0;
        infoRec->SetupForScanlineCPUToScreenColorExpandFill = NULL;
        infoRec->SubsequentScanlineCPUToScreenColorExpandFill = NULL;
        infoRec->SubsequentColorExpandScanline = NULL;
    }

    /**** Screen to Screen Color Expansion ****/

    if (infoRec->SetupForScreenToScreenColorExpandFill &&
        infoRec->SubsequentScreenToScreenColorExpandFill &&
        !xf86ReturnOptValBool(options, XAAOPT_SCREEN_TO_SCREEN_COL_EXP_FILL,
                              FALSE)) {
        HaveScreenToScreenColorExpandFill = TRUE;
        if (!infoRec->CacheColorExpandDensity)
            infoRec->CacheColorExpandDensity = 1;
    }
    else {
        infoRec->ScreenToScreenColorExpandFillFlags = 0;
        infoRec->SetupForScreenToScreenColorExpandFill = NULL;
        infoRec->SubsequentScreenToScreenColorExpandFill = NULL;
    }

    /**** Image Writes ****/

    if (infoRec->SetupForImageWrite && infoRec->ImageWriteBase &&
        infoRec->SubsequentImageWriteRect &&
        !xf86ReturnOptValBool(options, XAAOPT_IMAGE_WRITE_RECT, FALSE)) {

        infoRec->ImageWriteRange >>= 2; /* convert to DWORDS */
        if (infoRec->ImageWriteFlags & CPU_TRANSFER_BASE_FIXED)
            infoRec->ImageWriteRange = 0;
        HaveImageWriteRect = TRUE;
    }
    else {
        infoRec->ImageWriteFlags = 0;
        infoRec->SetupForImageWrite = NULL;
        infoRec->SubsequentImageWriteRect = NULL;
    }

    /**** Scanline Image Writes ****/

    if (infoRec->SetupForScanlineImageWrite &&
        infoRec->SubsequentScanlineImageWriteRect &&
        infoRec->SubsequentImageWriteScanline &&
        infoRec->ScanlineImageWriteBuffers &&
        (infoRec->NumScanlineImageWriteBuffers > 0) &&
        !xf86ReturnOptValBool(options, XAAOPT_SCANLINE_IMAGE_WRITE_RECT,
                              FALSE)) {
        HaveScanlineImageWriteRect = TRUE;
    }
    else {
        infoRec->ScanlineImageWriteFlags = 0;
        infoRec->SetupForScanlineImageWrite = NULL;
        infoRec->SubsequentScanlineImageWriteRect = NULL;
        infoRec->SubsequentImageWriteScanline = NULL;
    }

#ifndef __i386__
    /* XAA makes some unaligned accesses when clipping is not available */
#define CLIP_FLAGS (LEFT_EDGE_CLIPPING | LEFT_EDGE_CLIPPING_NEGATIVE_X)
    if (HaveImageWriteRect &&
        ((infoRec->ImageWriteFlags & CLIP_FLAGS) != CLIP_FLAGS)) {
        HaveImageWriteRect = FALSE;
    }
    if (HaveScanlineImageWriteRect &&
        ((infoRec->ScanlineImageWriteFlags & CLIP_FLAGS) != CLIP_FLAGS)) {
        HaveScanlineImageWriteRect = FALSE;
    }
#endif

    if (serverGeneration == 1) {
        if (HaveScreenToScreenCopy)
            xf86ErrorF("\tScreen to screen bit blits\n");
        if (HaveSolidFillRect)
            xf86ErrorF("\tSolid filled rectangles\n");
        if (HaveSolidFillTrap)
            xf86ErrorF("\tSolid filled trapezoids\n");
        if (HaveMono8x8PatternFillRect)
            xf86ErrorF("\t8x8 mono pattern filled rectangles\n");
        if (HaveMono8x8PatternFillTrap)
            xf86ErrorF("\t8x8 mono pattern filled trapezoids\n");
        if (HaveColor8x8PatternFillRect)
            xf86ErrorF("\t8x8 color pattern filled rectangles\n");
        if (HaveColor8x8PatternFillTrap)
            xf86ErrorF("\t8x8 color pattern filled trapezoids\n");

        if (HaveColorExpansion)
            xf86ErrorF("\tCPU to Screen color expansion\n");
        else if (HaveScanlineColorExpansion)
            xf86ErrorF("\tIndirect CPU to Screen color expansion\n");

        if (HaveScreenToScreenColorExpandFill)
            xf86ErrorF("\tScreen to Screen color expansion\n");

        if (HaveSolidTwoPointLine || HaveSolidBresenhamLine)
            xf86ErrorF("\tSolid Lines\n");
        else if (HaveSolidHorVertLine)
            xf86ErrorF("\tSolid Horizontal and Vertical Lines\n");

        if (HaveDashedTwoPointLine || HaveDashedBresenhamLine)
            xf86ErrorF("\tDashed Lines\n");

        if (HaveImageWriteRect)
            xf86ErrorF("\tImage Writes\n");
        else if (HaveScanlineImageWriteRect)
            xf86ErrorF("\tScanline Image Writes\n");

    }

#define XAAMSG(s) do { if (serverGeneration == 1) xf86ErrorF(s); } while (0)

    if ((infoRec->Flags & OFFSCREEN_PIXMAPS) && HaveScreenToScreenCopy &&
        xf86ReturnOptValBool(options,
                             XAAOPT_HAS_DUMB_INVERTED_OPTION_SENSE, FALSE)) {
        XAAMSG("\tOffscreen Pixmaps\n");
    }
    else {
        infoRec->Flags &= ~OFFSCREEN_PIXMAPS;
    }

    /************** Mid Level *************/

    /**** ScreenToScreenBitBlt ****/

    if (infoRec->ScreenToScreenBitBlt) {
        XAAMSG("\tDriver provided ScreenToScreenBitBlt replacement\n");
    }
    else if (HaveScreenToScreenCopy) {
        infoRec->ScreenToScreenBitBlt = XAAScreenToScreenBitBlt;
        infoRec->ScreenToScreenBitBltFlags = infoRec->ScreenToScreenCopyFlags;
    }

    /**** FillSolidRects ****/

    if (infoRec->FillSolidRects) {
        XAAMSG("\tDriver provided FillSolidRects replacement\n");
    }
    else if (HaveSolidFillRect) {
        infoRec->FillSolidRects = XAAFillSolidRects;
        infoRec->FillSolidRectsFlags = infoRec->SolidFillFlags;
    }

    /**** FillSolidSpans ****/

    if (infoRec->FillSolidSpans) {
        XAAMSG("\tDriver provided FillSolidSpans replacement\n");
    }
    else if (HaveSolidFillRect) {
        infoRec->FillSolidSpans = XAAFillSolidSpans;
        infoRec->FillSolidSpansFlags = infoRec->SolidFillFlags;
    }

    /**** FillMono8x8PatternRects ****/

    if (infoRec->FillMono8x8PatternRects) {
        XAAMSG("\tDriver provided FillMono8x8PatternRects replacement\n");
    }
    else if (HaveMono8x8PatternFillRect) {
        infoRec->FillMono8x8PatternRects =
            (infoRec->
             Mono8x8PatternFillFlags & HARDWARE_PATTERN_SCREEN_ORIGIN) ?
            XAAFillMono8x8PatternRectsScreenOrigin : XAAFillMono8x8PatternRects;

        infoRec->FillMono8x8PatternRectsFlags =
            infoRec->Mono8x8PatternFillFlags;
    }

    /**** FillMono8x8PatternSpans ****/

    if (infoRec->FillMono8x8PatternSpans) {
        XAAMSG("\tDriver provided FillMono8x8PatternSpans replacement\n");
    }
    else if (HaveMono8x8PatternFillRect) {
        infoRec->FillMono8x8PatternSpans =
            (infoRec->
             Mono8x8PatternFillFlags & HARDWARE_PATTERN_SCREEN_ORIGIN) ?
            XAAFillMono8x8PatternSpansScreenOrigin : XAAFillMono8x8PatternSpans;

        infoRec->FillMono8x8PatternSpansFlags =
            infoRec->Mono8x8PatternFillFlags;
    }

    /**** FillColor8x8Rects ****/

    if (infoRec->FillColor8x8PatternRects) {
        XAAMSG("\tDriver provided FillColor8x8PatternRects replacement\n");
    }
    else if (HaveColor8x8PatternFillRect) {
        infoRec->FillColor8x8PatternRects =
            (infoRec->
             Color8x8PatternFillFlags & HARDWARE_PATTERN_SCREEN_ORIGIN) ?
            XAAFillColor8x8PatternRectsScreenOrigin :
            XAAFillColor8x8PatternRects;

        infoRec->FillColor8x8PatternRectsFlags =
            infoRec->Color8x8PatternFillFlags;
    }

    /**** FillColor8x8Spans ****/

    if (infoRec->FillColor8x8PatternSpans) {
        XAAMSG("\tDriver provided FillColor8x8PatternSpans replacement\n");
    }
    else if (HaveColor8x8PatternFillRect) {
        infoRec->FillColor8x8PatternSpans =
            (infoRec->
             Color8x8PatternFillFlags & HARDWARE_PATTERN_SCREEN_ORIGIN) ?
            XAAFillColor8x8PatternSpansScreenOrigin :
            XAAFillColor8x8PatternSpans;

        infoRec->FillColor8x8PatternSpansFlags =
            infoRec->Color8x8PatternFillFlags;
    }

    /**** FillCacheBltRects ****/

    if (infoRec->FillCacheBltRects) {
        XAAMSG("\tDriver provided FillCacheBltRects replacement\n");
    }
    else if (HaveScreenToScreenCopy) {
        infoRec->FillCacheBltRects = XAAFillCacheBltRects;
        infoRec->FillCacheBltRectsFlags = infoRec->ScreenToScreenCopyFlags;
    }

    /**** FillCacheBltSpans ****/

    if (infoRec->FillCacheBltSpans) {
        XAAMSG("\tDriver provided FillCacheBltSpans replacement\n");
    }
    else if (HaveScreenToScreenCopy) {
        infoRec->FillCacheBltSpans = XAAFillCacheBltSpans;
        infoRec->FillCacheBltSpansFlags = infoRec->ScreenToScreenCopyFlags;
    }

    /**** FillCacheExpandRects ****/

    if (infoRec->FillCacheExpandRects) {
        XAAMSG("\tDriver provided FillCacheExpandRects replacement\n");
    }
    else if (HaveScreenToScreenColorExpandFill) {
        infoRec->FillCacheExpandRects = XAAFillCacheExpandRects;
        infoRec->FillCacheExpandRectsFlags =
            infoRec->ScreenToScreenColorExpandFillFlags;
    }

    /**** FillCacheExpandSpans ****/

    if (infoRec->FillCacheExpandSpans) {
        XAAMSG("\tDriver provided FillCacheExpandSpans replacement\n");
    }
    else if (HaveScreenToScreenColorExpandFill) {
        infoRec->FillCacheExpandSpans = XAAFillCacheExpandSpans;
        infoRec->FillCacheExpandSpansFlags =
            infoRec->ScreenToScreenColorExpandFillFlags;
    }

    /**** FillColorExpandRects ****/

    if (infoRec->FillColorExpandRects) {
        XAAMSG("\tDriver provided FillColorExpandRects replacement\n");
    }
    else if (HaveColorExpansion) {
        if (infoRec->CPUToScreenColorExpandFillFlags & TRIPLE_BITS_24BPP) {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRects3MSBFirstFixedBase;
                else
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRects3MSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRects3LSBFirstFixedBase;
                else
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRects3LSBFirst;
            }
        }
        else {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRectsMSBFirstFixedBase;
                else
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRectsMSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRectsLSBFirstFixedBase;
                else
                    infoRec->FillColorExpandRects =
                        XAAFillColorExpandRectsLSBFirst;
            }
        }
        infoRec->FillColorExpandRectsFlags =
            infoRec->CPUToScreenColorExpandFillFlags;
    }
    else if (HaveScanlineColorExpansion) {
        if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
            TRIPLE_BITS_24BPP) {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->FillColorExpandRects =
                    XAAFillScanlineColorExpandRects3MSBFirst;
            else
                infoRec->FillColorExpandRects =
                    XAAFillScanlineColorExpandRects3LSBFirst;
        }
        else {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->FillColorExpandRects =
                    XAAFillScanlineColorExpandRectsMSBFirst;
            else
                infoRec->FillColorExpandRects =
                    XAAFillScanlineColorExpandRectsLSBFirst;
        }
        infoRec->FillColorExpandRectsFlags =
            infoRec->ScanlineCPUToScreenColorExpandFillFlags;
    }

    /**** FillColorExpandSpans ****/

    if (infoRec->FillColorExpandSpans) {
        XAAMSG("\tDriver provided FillColorExpandSpans replacement\n");
    }
    else if (HaveColorExpansion) {
        if (infoRec->CPUToScreenColorExpandFillFlags & TRIPLE_BITS_24BPP) {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpans3MSBFirstFixedBase;
                else
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpans3MSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpans3LSBFirstFixedBase;
                else
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpans3LSBFirst;
            }
        }
        else {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpansMSBFirstFixedBase;
                else
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpansMSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpansLSBFirstFixedBase;
                else
                    infoRec->FillColorExpandSpans =
                        XAAFillColorExpandSpansLSBFirst;
            }
        }
        infoRec->FillColorExpandSpansFlags =
            infoRec->CPUToScreenColorExpandFillFlags;
    }
    else if (HaveScanlineColorExpansion) {
        if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
            TRIPLE_BITS_24BPP) {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->FillColorExpandSpans =
                    XAAFillScanlineColorExpandSpans3MSBFirst;
            else
                infoRec->FillColorExpandSpans =
                    XAAFillScanlineColorExpandSpans3LSBFirst;
        }
        else {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->FillColorExpandSpans =
                    XAAFillScanlineColorExpandSpansMSBFirst;
            else
                infoRec->FillColorExpandSpans =
                    XAAFillScanlineColorExpandSpansLSBFirst;
        }
        infoRec->FillColorExpandSpansFlags =
            infoRec->ScanlineCPUToScreenColorExpandFillFlags;
    }

    /**** FillImageWriteRects ****/

    if (infoRec->FillImageWriteRects) {
        XAAMSG("\tDriver provided FillImageWriteRects replacement\n");
    }
    else if (HaveImageWriteRect &&
             (infoRec->ImageWriteFlags & LEFT_EDGE_CLIPPING_NEGATIVE_X) &&
             (infoRec->ImageWriteFlags & LEFT_EDGE_CLIPPING)) {
        infoRec->FillImageWriteRects = XAAFillImageWriteRects;
        infoRec->FillImageWriteRectsFlags = infoRec->ImageWriteFlags;
    }

    /**** WriteBitmap ****/

    if (infoRec->WriteBitmap &&
        !xf86ReturnOptValBool(options, XAAOPT_WRITE_BITMAP, FALSE)) {
        XAAMSG("\tDriver provided WriteBitmap replacement\n");
    }
    else if (HaveColorExpansion) {
        if (infoRec->CPUToScreenColorExpandFillFlags & TRIPLE_BITS_24BPP) {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->WriteBitmap =
                        XAAWriteBitmapColorExpand3MSBFirstFixedBase;
                else
                    infoRec->WriteBitmap = XAAWriteBitmapColorExpand3MSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->WriteBitmap =
                        XAAWriteBitmapColorExpand3LSBFirstFixedBase;
                else
                    infoRec->WriteBitmap = XAAWriteBitmapColorExpand3LSBFirst;
            }
        }
        else {
            if (infoRec->CPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->WriteBitmap =
                        XAAWriteBitmapColorExpandMSBFirstFixedBase;
                else
                    infoRec->WriteBitmap = XAAWriteBitmapColorExpandMSBFirst;
            }
            else {
                if (infoRec->CPUToScreenColorExpandFillFlags &
                    CPU_TRANSFER_BASE_FIXED)
                    infoRec->WriteBitmap =
                        XAAWriteBitmapColorExpandLSBFirstFixedBase;
                else
                    infoRec->WriteBitmap = XAAWriteBitmapColorExpandLSBFirst;
            }
        }
        infoRec->WriteBitmapFlags = infoRec->CPUToScreenColorExpandFillFlags;
    }
    else if (HaveScanlineColorExpansion) {
        if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
            TRIPLE_BITS_24BPP) {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->WriteBitmap =
                    XAAWriteBitmapScanlineColorExpand3MSBFirst;
            else
                infoRec->WriteBitmap =
                    XAAWriteBitmapScanlineColorExpand3LSBFirst;
        }
        else {
            if (infoRec->ScanlineCPUToScreenColorExpandFillFlags &
                BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->WriteBitmap =
                    XAAWriteBitmapScanlineColorExpandMSBFirst;
            else
                infoRec->WriteBitmap =
                    XAAWriteBitmapScanlineColorExpandLSBFirst;
        }
        infoRec->WriteBitmapFlags =
            infoRec->ScanlineCPUToScreenColorExpandFillFlags;
    }
    else
        infoRec->WriteBitmap = NULL;

    /**** TE Glyphs ****/

    if (infoRec->TEGlyphRenderer) {
        XAAMSG("\tDriver provided TEGlyphRenderer replacement\n");
    }
    else if (HaveColorExpansion) {
        infoRec->TEGlyphRendererFlags =
            infoRec->CPUToScreenColorExpandFillFlags;

        if (infoRec->TEGlyphRendererFlags & TRIPLE_BITS_24BPP) {
            if (infoRec->TEGlyphRendererFlags & BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->TEGlyphRendererFlags & CPU_TRANSFER_BASE_FIXED)
                    infoRec->TEGlyphRenderer =
                        XAATEGlyphRenderer3MSBFirstFixedBase;
                else
                    infoRec->TEGlyphRenderer = XAATEGlyphRenderer3MSBFirst;
            }
            else {
                if (infoRec->TEGlyphRendererFlags & CPU_TRANSFER_BASE_FIXED)
                    infoRec->TEGlyphRenderer =
                        XAATEGlyphRenderer3LSBFirstFixedBase;
                else
                    infoRec->TEGlyphRenderer = XAATEGlyphRenderer3LSBFirst;
            }

            if (!HaveSolidFillRect &&
                (infoRec->TEGlyphRendererFlags & RGB_EQUAL)) {
                infoRec->TEGlyphRendererFlags &= ~RGB_EQUAL;
                XAAMSG("WARNING:  TEGlyphRenderer cannot support RGB_EQUAL"
                       " without solid fills\n");
            }
        }
        else {
            if (infoRec->TEGlyphRendererFlags & BIT_ORDER_IN_BYTE_MSBFIRST) {
                if (infoRec->TEGlyphRendererFlags & CPU_TRANSFER_BASE_FIXED)
                    infoRec->TEGlyphRenderer =
                        XAATEGlyphRendererMSBFirstFixedBase;
                else
                    infoRec->TEGlyphRenderer = XAATEGlyphRendererMSBFirst;
            }
            else {
                if (infoRec->TEGlyphRendererFlags & CPU_TRANSFER_BASE_FIXED)
                    infoRec->TEGlyphRenderer =
                        XAATEGlyphRendererLSBFirstFixedBase;
                else
                    infoRec->TEGlyphRenderer = XAATEGlyphRendererLSBFirst;
            }
        }

        if (!HaveSolidFillRect &&
            (infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY)) {
            infoRec->TEGlyphRendererFlags &= ~TRANSPARENCY_ONLY;
            XAAMSG("WARNING:  TEGlyphRenderer cannot support TRANPARENCY_ONLY"
                   " without solid fills\n");
        }

    }
    else if (HaveScanlineColorExpansion) {
        infoRec->TEGlyphRendererFlags =
            infoRec->ScanlineCPUToScreenColorExpandFillFlags;

        if (infoRec->TEGlyphRendererFlags & TRIPLE_BITS_24BPP) {
            if (infoRec->TEGlyphRendererFlags & BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->TEGlyphRenderer = XAATEGlyphRendererScanline3MSBFirst;
            else
                infoRec->TEGlyphRenderer = XAATEGlyphRendererScanline3LSBFirst;

            if (!HaveSolidFillRect &&
                (infoRec->TEGlyphRendererFlags & RGB_EQUAL)) {
                infoRec->TEGlyphRendererFlags &= ~RGB_EQUAL;
                XAAMSG("WARNING:  TEGlyphRenderer cannot support RGB_EQUAL"
                       " without solid fills\n");
            }
        }
        else {
            if (infoRec->TEGlyphRendererFlags & BIT_ORDER_IN_BYTE_MSBFIRST)
                infoRec->TEGlyphRenderer = XAATEGlyphRendererScanlineMSBFirst;
            else
                infoRec->TEGlyphRenderer = XAATEGlyphRendererScanlineLSBFirst;
        }

        if (!HaveSolidFillRect &&
            (infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY)) {
            infoRec->TEGlyphRendererFlags &= ~TRANSPARENCY_ONLY;
            XAAMSG("WARNING:  TEGlyphRenderer cannot support TRANPARENCY_ONLY"
                   " without solid fills\n");
        }
    }

    /**** NonTE Glyphs ****/

    if (infoRec->NonTEGlyphRenderer) {
        XAAMSG("\tDriver provided NonTEGlyphRenderer replacement\n");
    }
    else if (infoRec->WriteBitmap &&
             !(infoRec->WriteBitmapFlags & NO_TRANSPARENCY)) {
        infoRec->NonTEGlyphRenderer = XAANonTEGlyphRenderer;
        infoRec->NonTEGlyphRendererFlags = infoRec->WriteBitmapFlags;
    }

    /**** WritePixmap ****/

    if (infoRec->WritePixmap &&
        !xf86ReturnOptValBool(options, XAAOPT_WRITE_PIXMAP, FALSE)) {
        XAAMSG("\tDriver provided WritePixmap replacement\n");
    }
    else if (HaveImageWriteRect) {
        infoRec->WritePixmap = XAAWritePixmap;
        infoRec->WritePixmapFlags =
            infoRec->ImageWriteFlags | CONVERT_32BPP_TO_24BPP;
    }
    else if (HaveScanlineImageWriteRect) {
        infoRec->WritePixmap = XAAWritePixmapScanline;
        infoRec->WritePixmapFlags = infoRec->ScanlineImageWriteFlags;
    }
    else
        infoRec->WritePixmap = NULL;

    /**** ReadPixmap ****/

    if (infoRec->ReadPixmap) {
        XAAMSG("\tDriver provided ReadPixmap replacement\n");
    }

    /************** GC Level *************/

    /**** CopyArea ****/

    if (infoRec->CopyArea) {
        XAAMSG("\tDriver provided GC level CopyArea replacement\n");
    }
    else if (infoRec->ScreenToScreenBitBlt) {
        infoRec->CopyArea = XAACopyArea;
        infoRec->CopyAreaFlags = infoRec->ScreenToScreenBitBltFlags;

        /* most GC level primitives use one mid-level primitive so
           the GC level primitive gets the mid-level primitive flag
           and we use that at GC validation time.  But CopyArea uses
           more than one mid-level primitive so we have to essentially
           do a GC validation every time that primitive is used.
           The CopyAreaFlags would only be used for filtering out the
           common denominators.  Here we assume that if you don't do
           ScreenToScreenBitBlt you aren't going to do the others.
           We also assume that ScreenToScreenBitBlt has the least
           restrictions. */
    }

    if (infoRec->CopyPlane) {
        XAAMSG("\tDriver provided GC level CopyPlane replacement\n");
    }
    else if (infoRec->WriteBitmap &&
             !(infoRec->WriteBitmapFlags & TRANSPARENCY_ONLY)) {
        infoRec->CopyPlane = XAACopyPlaneColorExpansion;
        infoRec->CopyPlaneFlags = infoRec->WriteBitmapFlags;
    }

    if (infoRec->PushPixelsSolid) {
        XAAMSG("\tDriver provided GC level PushPixelsSolid replacement\n");
    }
    else if (infoRec->WriteBitmap &&
             !(infoRec->WriteBitmapFlags & NO_TRANSPARENCY)) {
        infoRec->PushPixelsSolid = XAAPushPixelsSolidColorExpansion;
        infoRec->PushPixelsFlags = infoRec->WriteBitmapFlags;
    }

    if (infoRec->FillSolidRects) {
        if (!infoRec->PolyFillRectSolid) {
            infoRec->PolyFillRectSolid = XAAPolyFillRect;
            infoRec->PolyFillRectSolidFlags = infoRec->FillSolidRectsFlags;
        }
    }
    if (infoRec->FillSolidSpans) {
        if (!infoRec->FillSpansSolid) {
            infoRec->FillSpansSolid = XAAFillSpans;
            infoRec->FillSpansSolidFlags = infoRec->FillSolidSpansFlags;
        }
    }

    if (infoRec->FillMono8x8PatternRects || infoRec->FillColor8x8PatternRects ||
        infoRec->FillCacheBltRects || infoRec->FillColorExpandRects ||
        infoRec->FillCacheExpandRects) {
        if (!infoRec->PolyFillRectStippled) {

            infoRec->PolyFillRectStippled = XAAPolyFillRect;
            infoRec->PolyFillRectStippledFlags = 0;
        }
    }

    if (infoRec->FillMono8x8PatternSpans || infoRec->FillColor8x8PatternSpans ||
        infoRec->FillCacheBltSpans || infoRec->FillColorExpandSpans ||
        infoRec->FillCacheExpandSpans) {
        if (!infoRec->FillSpansStippled) {

            infoRec->FillSpansStippled = XAAFillSpans;
            infoRec->FillSpansStippledFlags = 0;
        }
    }

    if (infoRec->FillMono8x8PatternRects || infoRec->FillColor8x8PatternRects ||
        infoRec->FillCacheBltRects || infoRec->FillColorExpandRects ||
        infoRec->FillCacheExpandRects) {
        if (!infoRec->PolyFillRectOpaqueStippled) {

            infoRec->PolyFillRectOpaqueStippled = XAAPolyFillRect;
            infoRec->PolyFillRectOpaqueStippledFlags = 0;
        }
    }

    if (infoRec->FillMono8x8PatternSpans || infoRec->FillColor8x8PatternSpans ||
        infoRec->FillCacheBltSpans || infoRec->FillColorExpandSpans ||
        infoRec->FillCacheExpandSpans) {
        if (!infoRec->FillSpansOpaqueStippled) {

            infoRec->FillSpansOpaqueStippled = XAAFillSpans;
            infoRec->FillSpansOpaqueStippledFlags = 0;
        }
    }

    if (infoRec->FillMono8x8PatternRects || infoRec->FillColor8x8PatternRects ||
        infoRec->FillCacheBltRects || infoRec->FillImageWriteRects) {
        if (!infoRec->PolyFillRectTiled) {

            infoRec->PolyFillRectTiled = XAAPolyFillRect;
            infoRec->PolyFillRectTiledFlags = 0;
        }
    }

    if (infoRec->FillMono8x8PatternSpans || infoRec->FillColor8x8PatternSpans ||
        infoRec->FillCacheBltSpans) {
        if (!infoRec->FillSpansTiled) {

            infoRec->FillSpansTiled = XAAFillSpans;
            infoRec->FillSpansTiledFlags = 0;
        }
    }

    if (infoRec->TEGlyphRenderer &&
        !(infoRec->TEGlyphRendererFlags & NO_TRANSPARENCY)) {

        if (!infoRec->PolyText8TE) {
            infoRec->PolyText8TE = XAAPolyText8TEColorExpansion;
            infoRec->PolyText8TEFlags = infoRec->TEGlyphRendererFlags;
        }

        if (!infoRec->PolyText16TE) {
            infoRec->PolyText16TE = XAAPolyText16TEColorExpansion;
            infoRec->PolyText16TEFlags = infoRec->TEGlyphRendererFlags;
        }

        if (!infoRec->PolyGlyphBltTE) {
            infoRec->PolyGlyphBltTE = XAAPolyGlyphBltTEColorExpansion;
            infoRec->PolyGlyphBltTEFlags = infoRec->TEGlyphRendererFlags;
        }
    }

    if (infoRec->TEGlyphRenderer &&
        !(infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY)) {

        if (!infoRec->ImageText8TE) {
            infoRec->ImageText8TE = XAAImageText8TEColorExpansion;
            infoRec->ImageText8TEFlags = infoRec->TEGlyphRendererFlags;
        }

        if (!infoRec->ImageText16TE) {
            infoRec->ImageText16TE = XAAImageText16TEColorExpansion;
            infoRec->ImageText16TEFlags = infoRec->TEGlyphRendererFlags;
        }

        if (!infoRec->ImageGlyphBltTE) {
            infoRec->ImageGlyphBltTE = XAAImageGlyphBltTEColorExpansion;
            infoRec->ImageGlyphBltTEFlags = infoRec->TEGlyphRendererFlags;
        }
    }

    if (infoRec->NonTEGlyphRenderer) {
        if (!infoRec->PolyText8NonTE) {
            infoRec->PolyText8NonTE = XAAPolyText8NonTEColorExpansion;
            infoRec->PolyText8NonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }

        if (!infoRec->PolyText16NonTE) {
            infoRec->PolyText16NonTE = XAAPolyText16NonTEColorExpansion;
            infoRec->PolyText16NonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }
        if (!infoRec->PolyGlyphBltNonTE) {
            infoRec->PolyGlyphBltNonTE = XAAPolyGlyphBltNonTEColorExpansion;
            infoRec->PolyGlyphBltNonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }
    }

    if (infoRec->NonTEGlyphRenderer && HaveSolidFillRect) {
        if (!infoRec->ImageText8NonTE) {
            infoRec->ImageText8NonTE = XAAImageText8NonTEColorExpansion;
            infoRec->ImageText8NonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }

        if (!infoRec->ImageText16NonTE) {
            infoRec->ImageText16NonTE = XAAImageText16NonTEColorExpansion;
            infoRec->ImageText16NonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }

        if (!infoRec->ImageGlyphBltNonTE) {
            infoRec->ImageGlyphBltNonTE = XAAImageGlyphBltNonTEColorExpansion;
            infoRec->ImageGlyphBltNonTEFlags = infoRec->NonTEGlyphRendererFlags;
        }
    }

    if (!infoRec->PolyRectangleThinSolid && HaveSolidHorVertLine) {
        infoRec->PolyRectangleThinSolid = XAAPolyRectangleThinSolid;
        infoRec->PolyRectangleThinSolidFlags = infoRec->SolidLineFlags;
    }

    if (!infoRec->FillPolygonSolid && HaveSolidFillRect) {
        infoRec->FillPolygonSolid = XAAFillPolygonSolid;
        infoRec->FillPolygonSolidFlags = infoRec->SolidFillFlags;
    }

    if (!infoRec->FillPolygonStippled && (HaveMono8x8PatternFillRect ||
                                          HaveScreenToScreenColorExpandFill ||
                                          HaveScreenToScreenCopy)) {
        infoRec->FillPolygonStippled = XAAFillPolygonStippled;
        infoRec->FillPolygonStippledFlags = infoRec->SolidFillFlags;
    }

    if (!infoRec->FillPolygonOpaqueStippled && (HaveMono8x8PatternFillRect ||
                                                HaveScreenToScreenColorExpandFill
                                                || HaveScreenToScreenCopy)) {
        infoRec->FillPolygonOpaqueStippled = XAAFillPolygonStippled;
        infoRec->FillPolygonOpaqueStippledFlags = infoRec->SolidFillFlags;
    }

    if (!infoRec->FillPolygonTiled && (HaveMono8x8PatternFillRect ||
                                       HaveScreenToScreenColorExpandFill ||
                                       HaveScreenToScreenCopy)) {
        infoRec->FillPolygonTiled = XAAFillPolygonTiled;
        infoRec->FillPolygonTiledFlags = infoRec->SolidFillFlags;
    }

    if (!infoRec->PolyFillArcSolid && HaveSolidFillRect) {
        infoRec->PolyFillArcSolid = XAAPolyFillArcSolid;
        infoRec->PolyFillArcSolidFlags = infoRec->SolidFillFlags;
    }

    if (!infoRec->PolylinesWideSolid && HaveSolidFillRect) {
        infoRec->PolylinesWideSolid = XAAPolylinesWideSolid;
        infoRec->PolylinesWideSolidFlags =
            infoRec->SolidFillFlags | GXCOPY_ONLY;
    }

    if (!infoRec->PutImage && (infoRec->WritePixmap ||
                               (infoRec->WriteBitmap &&
                                !(infoRec->
                                  WriteBitmapFlags & TRANSPARENCY_ONLY)))) {
        infoRec->PutImage = XAAPutImage;

        /* See comment for CopyArea above.  But here we make fewer 
           assumptions.  The driver can provide the PutImageFlags if
           it wants too */
    }

    if (HaveSolidHorVertLine &&
        (HaveSolidBresenhamLine || (HaveSolidTwoPointLine &&
                                    (infoRec->
                                     ClippingFlags &
                                     HARDWARE_CLIP_SOLID_LINE)))) {
        if (!infoRec->PolylinesThinSolid) {
            infoRec->PolylinesThinSolid = XAAPolyLines;
            infoRec->PolylinesThinSolidFlags = infoRec->SolidLineFlags;
        }
        if (!infoRec->PolySegmentThinSolid) {
            infoRec->PolySegmentThinSolid = XAAPolySegment;
            infoRec->PolySegmentThinSolidFlags = infoRec->SolidLineFlags;
        }
    }

    if (HaveDashedBresenhamLine || (HaveDashedTwoPointLine &&
                                    (infoRec->
                                     ClippingFlags &
                                     HARDWARE_CLIP_DASHED_LINE))) {
        if (!infoRec->PolylinesThinDashed) {
            infoRec->PolylinesThinDashed = XAAPolyLinesDashed;
            infoRec->PolylinesThinDashedFlags = infoRec->DashedLineFlags;
        }
        if (!infoRec->PolySegmentThinDashed) {
            infoRec->PolySegmentThinDashed = XAAPolySegmentDashed;
            infoRec->PolySegmentThinDashedFlags = infoRec->DashedLineFlags;
        }
    }

    if (infoRec->PolylinesThinDashed || infoRec->PolySegmentThinDashed) {
        if (!infoRec->ComputeDash)
            infoRec->ComputeDash = XAAComputeDash;
    }

    {
        Bool haveTexture = infoRec->CPUToScreenTextureFormats &&
            infoRec->CPUToScreenTextureDstFormats &&
            infoRec->SetupForCPUToScreenTexture2 &&
            infoRec->SubsequentCPUToScreenTexture;
        Bool haveAlphaTexture = infoRec->CPUToScreenAlphaTextureFormats &&
            infoRec->CPUToScreenAlphaTextureDstFormats &&
            infoRec->SetupForCPUToScreenAlphaTexture2 &&
            infoRec->SubsequentCPUToScreenAlphaTexture;

        if (!infoRec->Composite && (haveTexture || haveAlphaTexture))
            infoRec->Composite = XAADoComposite;

        if (!infoRec->Glyphs && infoRec->WriteBitmap &&
            !(infoRec->WriteBitmapFlags & NO_TRANSPARENCY)) {
            infoRec->Glyphs = XAADoGlyphs;
        }
    }

    /************  Validation Functions **************/

    if (!infoRec->ValidateCopyArea && infoRec->CopyArea) {
        infoRec->CopyAreaMask = GCWhenForced;
        if ((infoRec->CopyAreaFlags & GXCOPY_ONLY) ||
            (infoRec->CopyAreaFlags & ROP_NEEDS_SOURCE))
            infoRec->CopyAreaMask |= GCFunction;
        if (infoRec->CopyAreaFlags & NO_PLANEMASK)
            infoRec->CopyAreaMask |= GCPlaneMask;
        infoRec->ValidateCopyArea = XAAValidateCopyArea;
    }

    if (!infoRec->ValidateCopyPlane && infoRec->CopyPlane) {
        infoRec->CopyPlaneMask = GCWhenForced;
        if ((infoRec->CopyPlaneFlags & GXCOPY_ONLY) ||
            (infoRec->CopyPlaneFlags & ROP_NEEDS_SOURCE))
            infoRec->CopyPlaneMask |= GCFunction;
        if (infoRec->CopyPlaneFlags & NO_PLANEMASK)
            infoRec->CopyPlaneMask |= GCPlaneMask;
        if (infoRec->CopyPlaneFlags & RGB_EQUAL)
            infoRec->CopyPlaneMask |= GCForeground | GCBackground;
        infoRec->ValidateCopyPlane = XAAValidateCopyPlane;
    }

    if (!infoRec->ValidatePutImage && infoRec->PutImage) {
        infoRec->PutImageMask = GCWhenForced;
        if ((infoRec->PutImageFlags & GXCOPY_ONLY) ||
            (infoRec->PutImageFlags & ROP_NEEDS_SOURCE))
            infoRec->PutImageMask |= GCFunction;
        if (infoRec->PutImageFlags & NO_PLANEMASK)
            infoRec->PutImageMask |= GCPlaneMask;
        if (infoRec->PutImageFlags & RGB_EQUAL)
            infoRec->PutImageMask |= GCForeground | GCBackground;
        infoRec->ValidatePutImage = XAAValidatePutImage;
    }

    if (!infoRec->ValidatePushPixels && infoRec->PushPixelsSolid) {
        infoRec->PushPixelsMask = GCFillStyle;
        if ((infoRec->PushPixelsFlags & GXCOPY_ONLY) ||
            (infoRec->PushPixelsFlags & ROP_NEEDS_SOURCE) ||
            (infoRec->PushPixelsFlags & TRANSPARENCY_GXCOPY_ONLY))
            infoRec->PushPixelsMask |= GCFunction;
        if (infoRec->PushPixelsFlags & NO_PLANEMASK)
            infoRec->PushPixelsMask |= GCPlaneMask;
        if (infoRec->PushPixelsFlags & RGB_EQUAL)
            infoRec->PushPixelsMask |= GCForeground;
        infoRec->ValidatePushPixels = XAAValidatePushPixels;
    }

    /* By default XAA assumes the FillSpans, PolyFillRects, FillPolygon
       and PolyFillArcs have the same restrictions.  If you supply GC 
       level replacements for any of these and alter this relationship 
       you may need to supply replacement validation routines */

    if (!infoRec->ValidateFillSpans &&
        (infoRec->FillSpansSolid || infoRec->FillSpansStippled ||
         infoRec->FillSpansOpaqueStippled || infoRec->FillSpansTiled)) {

        int compositeFlags = infoRec->FillSpansSolidFlags |
            infoRec->FillSpansStippledFlags |
            infoRec->FillSpansOpaqueStippledFlags |
            infoRec->FillSpansTiledFlags;

        infoRec->FillSpansMask = GCFillStyle | GCTile | GCStipple;

        if ((compositeFlags & GXCOPY_ONLY) ||
            (compositeFlags & ROP_NEEDS_SOURCE))
            infoRec->FillSpansMask |= GCFunction;
        if (compositeFlags & NO_PLANEMASK)
            infoRec->FillSpansMask |= GCPlaneMask;
        if (compositeFlags & RGB_EQUAL)
            infoRec->FillSpansMask |= GCForeground;
        infoRec->ValidateFillSpans = XAAValidateFillSpans;
    }

    /* By default XAA only provides Validations for the GlyphBlt
       functions and not the text higher up. This is because the
       Text8/16 and GlyphBlt are linked.  If you break this linkage,
       you may need to have the driver supply its own Validation
       routines */

    if (!infoRec->ValidatePolyGlyphBlt &&
        (infoRec->PolyGlyphBltTE || infoRec->PolyGlyphBltNonTE)) {
        int compositeFlags = infoRec->PolyGlyphBltTEFlags |
            infoRec->PolyGlyphBltNonTEFlags;

        infoRec->PolyGlyphBltMask = GCFillStyle | GCFont;
        if ((compositeFlags & GXCOPY_ONLY) ||
            (compositeFlags & ROP_NEEDS_SOURCE) ||
            (infoRec->PolyGlyphBltNonTEFlags & TRANSPARENCY_GXCOPY_ONLY))
            infoRec->PolyGlyphBltMask |= GCFunction;
        if (compositeFlags & NO_PLANEMASK)
            infoRec->PolyGlyphBltMask |= GCPlaneMask;
        if (compositeFlags & RGB_EQUAL)
            infoRec->PolyGlyphBltMask |= GCForeground;
        infoRec->ValidatePolyGlyphBlt = XAAValidatePolyGlyphBlt;
    }

    if (!infoRec->ValidateImageGlyphBlt &&
        (infoRec->ImageGlyphBltTE || infoRec->ImageGlyphBltNonTE)) {
        int compositeFlags = infoRec->ImageGlyphBltTEFlags |
            infoRec->ImageGlyphBltNonTEFlags;

        if (infoRec->ImageGlyphBltNonTE)
            compositeFlags |= infoRec->SolidFillFlags;

        infoRec->ImageGlyphBltMask = GCFont;
        if (compositeFlags & NO_PLANEMASK)
            infoRec->ImageGlyphBltMask |= GCPlaneMask;
        if (compositeFlags & RGB_EQUAL)
            infoRec->ImageGlyphBltMask |= GCForeground | GCBackground;
        infoRec->ValidateImageGlyphBlt = XAAValidateImageGlyphBlt;
    }

    /* By default XAA only provides a Validation function for the 
       Polylines and does segments and polylines at the same time */

    if (!infoRec->ValidatePolylines && infoRec->ValidateFillSpans) {
        int compositeFlags = infoRec->PolyRectangleThinSolidFlags |
            infoRec->PolylinesWideSolidFlags |
            infoRec->PolylinesThinSolidFlags |
            infoRec->PolySegmentThinSolidFlags |
            infoRec->PolySegmentThinDashedFlags |
            infoRec->PolylinesThinDashedFlags;

        infoRec->ValidatePolylines = XAAValidatePolylines;
        infoRec->PolylinesMask =
            infoRec->FillSpansMask | GCLineStyle | GCLineWidth;

        if (infoRec->PolySegmentThinDashed || infoRec->PolylinesThinDashed)
            infoRec->PolylinesMask |= GCDashList;
        if (compositeFlags & NO_PLANEMASK)
            infoRec->PolylinesMask |= GCPlaneMask;
        if ((compositeFlags & GXCOPY_ONLY) ||
            (compositeFlags & ROP_NEEDS_SOURCE))
            infoRec->PolylinesMask |= GCFunction;
        if (compositeFlags & RGB_EQUAL)
            infoRec->PolylinesMask |= GCForeground;
    }

    /**** Fill choosers ****/

    if (!infoRec->StippledFillChooser)
        infoRec->StippledFillChooser = XAAStippledFillChooser;

    if (!infoRec->OpaqueStippledFillChooser)
        infoRec->OpaqueStippledFillChooser = XAAOpaqueStippledFillChooser;

    if (!infoRec->TiledFillChooser)
        infoRec->TiledFillChooser = XAATiledFillChooser;

    /**** Setup the pixmap cache ****/

    if (infoRec->WriteBitmapToCache) {
    }
    else if (infoRec->WriteBitmap &&
             !(infoRec->WriteBitmapFlags & TRANSPARENCY_ONLY))
        infoRec->WriteBitmapToCache = XAAWriteBitmapToCache;
    else if (infoRec->Flags & LINEAR_FRAMEBUFFER)
        infoRec->WriteBitmapToCache = XAAWriteBitmapToCacheLinear;
    else
        infoRec->PixmapCacheFlags |= DO_NOT_BLIT_STIPPLES;

    if (infoRec->WritePixmapToCache) {
    }
    else if (infoRec->WritePixmap && !(infoRec->WritePixmapFlags & NO_GXCOPY))
        infoRec->WritePixmapToCache = XAAWritePixmapToCache;
    else if (infoRec->Flags & LINEAR_FRAMEBUFFER)
        infoRec->WritePixmapToCache = XAAWritePixmapToCacheLinear;
    else
        infoRec->Flags &= ~PIXMAP_CACHE;

    if (xf86ReturnOptValBool(options, XAAOPT_PIXMAP_CACHE, FALSE))
        infoRec->Flags &= ~PIXMAP_CACHE;

    if (infoRec->WriteMono8x8PatternToCache) {
    }
    else if (infoRec->PixmapCacheFlags & CACHE_MONO_8x8) {
        if (infoRec->WritePixmapToCache)
            infoRec->WriteMono8x8PatternToCache = XAAWriteMono8x8PatternToCache;
        else
            infoRec->PixmapCacheFlags &= ~CACHE_MONO_8x8;
    }

    if (infoRec->WriteColor8x8PatternToCache) {
    }
    else if (infoRec->PixmapCacheFlags & CACHE_COLOR_8x8) {
        if (infoRec->WritePixmapToCache && infoRec->WriteBitmapToCache)
            infoRec->WriteColor8x8PatternToCache =
                XAAWriteColor8x8PatternToCache;
        else
            infoRec->PixmapCacheFlags &= ~CACHE_COLOR_8x8;
    }

    if (infoRec->CachePixelGranularity < 0) {
        switch (pScrn->bitsPerPixel) {
        case 24:
        case 8:
            infoRec->CachePixelGranularity = 4;
            break;
        case 16:
            infoRec->CachePixelGranularity = 2;
            break;
        case 32:
            infoRec->CachePixelGranularity = 1;
            break;
        default:
            break;
        }

        if (BITMAP_SCANLINE_PAD == 64)
            infoRec->CachePixelGranularity *= 2;
    }

    free(options);

    if (!infoRec->CacheTile && infoRec->WritePixmapToCache)
        infoRec->CacheTile = XAACacheTile;
    if (!infoRec->CacheMonoStipple && infoRec->WritePixmapToCache)
        infoRec->CacheMonoStipple = XAACacheMonoStipple;
    if (!infoRec->CacheStipple && infoRec->WriteBitmapToCache)
        infoRec->CacheStipple = XAACacheStipple;
    if (!infoRec->CacheMono8x8Pattern && infoRec->WriteMono8x8PatternToCache)
        infoRec->CacheMono8x8Pattern = XAACacheMono8x8Pattern;
    if (!infoRec->CacheColor8x8Pattern && infoRec->WriteColor8x8PatternToCache)
        infoRec->CacheColor8x8Pattern = XAACacheColor8x8Pattern;

    if ((infoRec->Flags & PIXMAP_CACHE) && !infoRec->InitPixmapCache) {
        infoRec->InitPixmapCache = XAAInitPixmapCache;
        infoRec->ClosePixmapCache = XAAClosePixmapCache;
    }

    return TRUE;
}
