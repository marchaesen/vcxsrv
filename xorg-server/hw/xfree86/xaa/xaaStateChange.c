
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "xf86str.h"
#include "mi.h"
#include "miline.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"
#include "servermd.h"

#define XAA_STATE_WRAP(func) do {\
if(infoRec->func) { \
   pStatePriv->func = infoRec->func;\
   infoRec->func = XAAStateWrap##func;\
}} while(0)

/* Wrap all XAA functions and allocate our private structure.
 */

typedef struct _XAAStateWrapRec {
    ScrnInfoPtr pScrn;
    void (*RestoreAccelState) (ScrnInfoPtr pScrn);
    void (*Sync) (ScrnInfoPtr pScrn);
    void (*SetupForScreenToScreenCopy) (ScrnInfoPtr pScrn, int xdir, int ydir,
                                        int rop, unsigned int planemask,
                                        int trans_color);
    void (*SetupForSolidFill) (ScrnInfoPtr pScrn, int color, int rop,
                               unsigned int planemask);
    void (*SetupForSolidLine) (ScrnInfoPtr pScrn, int color, int rop,
                               unsigned int planemask);
    void (*SetupForDashedLine) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                unsigned int planemask, int length,
                                unsigned char *pattern);
    void (*SetClippingRectangle) (ScrnInfoPtr pScrn, int left, int top,
                                  int right, int bottom);
    void (*DisableClipping) (ScrnInfoPtr pScrn);
    void (*SetupForMono8x8PatternFill) (ScrnInfoPtr pScrn, int patx, int paty,
                                        int fg, int bg, int rop,
                                        unsigned int planemask);
    void (*SetupForColor8x8PatternFill) (ScrnInfoPtr pScrn, int patx, int paty,
                                         int rop, unsigned int planemask,
                                         int transparency_color);
    void (*SetupForCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn, int fg,
                                                int bg, int rop,
                                                unsigned int planemask);
    void (*SetupForScanlineCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                        int fg, int bg, int rop,
                                                        unsigned int planemask);
    void (*SetupForScreenToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                   int fg, int bg, int rop,
                                                   unsigned int planemask);
    void (*SetupForImageWrite) (ScrnInfoPtr pScrn, int rop,
                                unsigned int planemask, int transparency_color,
                                int bpp, int depth);
    void (*SetupForScanlineImageWrite) (ScrnInfoPtr pScrn, int rop,
                                        unsigned int planemask,
                                        int transparency_color,
                                        int bpp, int depth);
    void (*SetupForImageRead) (ScrnInfoPtr pScrn, int bpp, int depth);
    void (*ScreenToScreenBitBlt) (ScrnInfoPtr pScrn, int nbox,
                                  DDXPointPtr pptSrc, BoxPtr pbox, int xdir,
                                  int ydir, int alu, unsigned int planmask);
    void (*WriteBitmap) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                         unsigned char *src, int srcwidth, int skipleft,
                         int fg, int bg, int rop, unsigned int planemask);
    void (*FillSolidRects) (ScrnInfoPtr pScrn, int fg, int rop,
                            unsigned int planemask, int nBox, BoxPtr pBox);
    void (*FillMono8x8PatternRects) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                     unsigned int planemask, int nBox,
                                     BoxPtr pBox, int pat0, int pat1,
                                     int xorg, int yorg);
    void (*FillColor8x8PatternRects) (ScrnInfoPtr pScrn, int rop,
                                      unsigned int planemask, int nBox,
                                      BoxPtr pBox, int xorg, int yorg,
                                      XAACacheInfoPtr pCache);
    void (*FillCacheBltRects) (ScrnInfoPtr pScrn, int rop,
                               unsigned int planemask, int nBox, BoxPtr pBox,
                               int xorg, int yorg, XAACacheInfoPtr pCache);
    void (*FillColorExpandRects) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                  unsigned int planemask, int nBox,
                                  BoxPtr pBox, int xorg, int yorg,
                                  PixmapPtr pPix);
    void (*FillCacheExpandRects) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                  unsigned int planemask, int nBox, BoxPtr pBox,
                                  int xorg, int yorg, PixmapPtr pPix);
    void (*FillImageWriteRects) (ScrnInfoPtr pScrn, int rop,
                                 unsigned int planemask, int nBox, BoxPtr pBox,
                                 int xorg, int yorg, PixmapPtr pPix);
    void (*FillSolidSpans) (ScrnInfoPtr pScrn, int fg, int rop,
                            unsigned int planemask, int n, DDXPointPtr points,
                            int *widths, int fSorted);
    void (*FillMono8x8PatternSpans) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                     unsigned int planemask, int n,
                                     DDXPointPtr points, int *widths,
                                     int fSorted, int pat0, int pat1,
                                     int xorg, int yorg);
    void (*FillColor8x8PatternSpans) (ScrnInfoPtr pScrn, int rop,
                                      unsigned int planemask, int n,
                                      DDXPointPtr points, int *widths,
                                      int fSorted, XAACacheInfoPtr pCache,
                                      int xorg, int yorg);
    void (*FillCacheBltSpans) (ScrnInfoPtr pScrn, int rop,
                               unsigned int planemask, int n,
                               DDXPointPtr points, int *widths, int fSorted,
                               XAACacheInfoPtr pCache, int xorg, int yorg);
    void (*FillColorExpandSpans) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                  unsigned int planemask, int n,
                                  DDXPointPtr points, int *widths, int fSorted,
                                  int xorg, int yorg, PixmapPtr pPix);
    void (*FillCacheExpandSpans) (ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                  unsigned int planemask, int n,
                                  DDXPointPtr ppt, int *pwidth, int fSorted,
                                  int xorg, int yorg, PixmapPtr pPix);
    void (*TEGlyphRenderer) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                             int skipleft, int startline, unsigned int **glyphs,
                             int glyphWidth, int fg, int bg, int rop,
                             unsigned planemask);
    void (*NonTEGlyphRenderer) (ScrnInfoPtr pScrn, int x, int y, int n,
                                NonTEGlyphPtr glyphs, BoxPtr pbox, int fg,
                                int rop, unsigned int planemask);
    void (*WritePixmap) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                         unsigned char *src, int srcwidth, int rop,
                         unsigned int planemask, int transparency_color,
                         int bpp, int depth);
    void (*ReadPixmap) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                        unsigned char *dst, int dstwidth, int bpp, int depth);
    RegionPtr (*CopyArea) (DrawablePtr pSrcDrawable, DrawablePtr pDstDrawable,
                           GC * pGC, int srcx, int srcy, int width, int height,
                           int dstx, int dsty);
    RegionPtr (*CopyPlane) (DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                            int srcx, int srcy, int width, int height, int dstx,
                            int dsty, unsigned long bitPlane);
    void (*PushPixelsSolid) (GCPtr pGC, PixmapPtr pBitMap,
                             DrawablePtr pDrawable, int dx, int dy, int xOrg,
                             int yOrg);
    void (*PolyFillRectSolid) (DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                               xRectangle *prectInit);
    void (*PolyFillRectStippled) (DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                                  xRectangle *prectInit);
    void (*PolyFillRectOpaqueStippled) (DrawablePtr pDraw, GCPtr pGC,
                                        int nrectFill, xRectangle *prectInit);
    void (*PolyFillRectTiled) (DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                               xRectangle *prectInit);
    void (*FillSpansSolid) (DrawablePtr pDraw, GCPtr pGC, int nInit,
                            DDXPointPtr ppt, int *pwidth, int fSorted);
    void (*FillSpansStippled) (DrawablePtr pDraw, GCPtr pGC, int nInit,
                               DDXPointPtr ppt, int *pwidth, int fSorted);
    void (*FillSpansOpaqueStippled) (DrawablePtr pDraw, GCPtr pGC, int nInit,
                                     DDXPointPtr ppt, int *pwidth, int fSorted);
    void (*FillSpansTiled) (DrawablePtr pDraw, GCPtr pGC, int nInit,
                            DDXPointPtr ppt, int *pwidth, int fSorted);
    int (*PolyText8TE) (DrawablePtr pDraw, GCPtr pGC, int x, int y, int count,
                        char *chars);
    int (*PolyText16TE) (DrawablePtr pDraw, GCPtr pGC, int x, int y, int count,
                         unsigned short *chars);
    void (*ImageText8TE) (DrawablePtr pDraw, GCPtr pGC, int x, int y, int count,
                          char *chars);
    void (*ImageText16TE) (DrawablePtr pDraw, GCPtr pGC, int x, int y,
                           int count, unsigned short *chars);
    void (*ImageGlyphBltTE) (DrawablePtr pDrawable, GCPtr pGC, int xInit,
                             int yInit, unsigned int nglyph, CharInfoPtr * ppci,
                             pointer pglyphBase);
    void (*PolyGlyphBltTE) (DrawablePtr pDrawable, GCPtr pGC, int xInit,
                            int yInit, unsigned int nglyph, CharInfoPtr * ppci,
                            pointer pglyphBase);
    int (*PolyText8NonTE) (DrawablePtr pDraw, GCPtr pGC, int x, int y,
                           int count, char *chars);
    int (*PolyText16NonTE) (DrawablePtr pDraw, GCPtr pGC, int x, int y,
                            int count, unsigned short *chars);
    void (*ImageText8NonTE) (DrawablePtr pDraw, GCPtr pGC, int x, int y,
                             int count, char *chars);
    void (*ImageText16NonTE) (DrawablePtr pDraw, GCPtr pGC, int x, int y,
                              int count, unsigned short *chars);
    void (*ImageGlyphBltNonTE) (DrawablePtr pDrawable, GCPtr pGC, int xInit,
                                int yInit, unsigned int nglyph,
                                CharInfoPtr * ppci, pointer pglyphBase);
    void (*PolyGlyphBltNonTE) (DrawablePtr pDrawable, GCPtr pGC, int xInit,
                               int yInit, unsigned int nglyph,
                               CharInfoPtr * ppci, pointer pglyphBase);
    void (*PolyRectangleThinSolid) (DrawablePtr pDrawable, GCPtr pGC,
                                    int nRectsInit, xRectangle *pRectsInit);
    void (*PolylinesWideSolid) (DrawablePtr pDrawable, GCPtr pGC, int mode,
                                int npt, DDXPointPtr pPts);
    void (*PolylinesThinSolid) (DrawablePtr pDrawable, GCPtr pGC, int mode,
                                int npt, DDXPointPtr pPts);
    void (*PolySegmentThinSolid) (DrawablePtr pDrawable, GCPtr pGC, int nseg,
                                  xSegment * pSeg);
    void (*PolylinesThinDashed) (DrawablePtr pDrawable, GCPtr pGC, int mode,
                                 int npt, DDXPointPtr pPts);
    void (*PolySegmentThinDashed) (DrawablePtr pDrawable, GCPtr pGC, int nseg,
                                   xSegment * pSeg);
    void (*FillPolygonSolid) (DrawablePtr pDrawable, GCPtr pGC, int shape,
                              int mode, int count, DDXPointPtr ptsIn);
    void (*FillPolygonStippled) (DrawablePtr pDrawable, GCPtr pGC, int shape,
                                 int mode, int count, DDXPointPtr ptsIn);
    void (*FillPolygonOpaqueStippled) (DrawablePtr pDrawable, GCPtr pGC,
                                       int shape, int mode, int count,
                                       DDXPointPtr ptsIn);
    void (*FillPolygonTiled) (DrawablePtr pDrawable, GCPtr pGC, int shape,
                              int mode, int count, DDXPointPtr ptsIn);
    void (*PolyFillArcSolid) (DrawablePtr pDraw, GCPtr pGC, int narcs,
                              xArc * parcs);
    void (*PutImage) (DrawablePtr pDraw, GCPtr pGC, int depth, int x, int y,
                      int w, int h, int leftPad, int format, char *pImage);
    ValidateGCProcPtr ValidateFillSpans;
    ValidateGCProcPtr ValidateSetSpans;
    ValidateGCProcPtr ValidatePutImage;
    ValidateGCProcPtr ValidateCopyArea;
    ValidateGCProcPtr ValidateCopyPlane;
    ValidateGCProcPtr ValidatePolyPoint;
    ValidateGCProcPtr ValidatePolylines;
    ValidateGCProcPtr ValidatePolySegment;
    ValidateGCProcPtr ValidatePolyRectangle;
    ValidateGCProcPtr ValidatePolyArc;
    ValidateGCProcPtr ValidateFillPolygon;
    ValidateGCProcPtr ValidatePolyFillRect;
    ValidateGCProcPtr ValidatePolyFillArc;
    ValidateGCProcPtr ValidatePolyText8;
    ValidateGCProcPtr ValidatePolyText16;
    ValidateGCProcPtr ValidateImageText8;
    ValidateGCProcPtr ValidateImageText16;
    ValidateGCProcPtr ValidatePolyGlyphBlt;
    ValidateGCProcPtr ValidateImageGlyphBlt;
    ValidateGCProcPtr ValidatePushPixels;
    void (*ComputeDash) (GCPtr pGC);
    void (*InitPixmapCache) (ScreenPtr pScreen, RegionPtr areas, pointer data);
    void (*ClosePixmapCache) (ScreenPtr pScreen);
    int (*StippledFillChooser) (GCPtr pGC);
    int (*OpaqueStippledFillChooser) (GCPtr pGC);
    int (*TiledFillChooser) (GCPtr pGC);
     XAACacheInfoPtr(*CacheTile) (ScrnInfoPtr Scrn, PixmapPtr pPix);
     XAACacheInfoPtr(*CacheStipple) (ScrnInfoPtr Scrn, PixmapPtr pPix, int fg,
                                     int bg);
     XAACacheInfoPtr(*CacheMonoStipple) (ScrnInfoPtr Scrn, PixmapPtr pPix);
     XAACacheInfoPtr(*CacheMono8x8Pattern) (ScrnInfoPtr Scrn, int pat0,
                                            int pat1);
     XAACacheInfoPtr(*CacheColor8x8Pattern) (ScrnInfoPtr Scrn, PixmapPtr pPix,
                                             int fg, int bg);
    void (*WriteBitmapToCache) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                                unsigned char *src, int srcwidth, int fg,
                                int bg);
    void (*WritePixmapToCache) (ScrnInfoPtr pScrn, int x, int y, int w, int h,
                                unsigned char *src, int srcwidth, int bpp,
                                int depth);
    void (*WriteMono8x8PatternToCache) (ScrnInfoPtr pScrn,
                                        XAACacheInfoPtr pCache);
    void (*WriteColor8x8PatternToCache) (ScrnInfoPtr pScrn, PixmapPtr pPix,
                                         XAACacheInfoPtr pCache);
    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;
    CopyWindowProcPtr CopyWindow;
    Bool (*SetupForCPUToScreenAlphaTexture2) (ScrnInfoPtr pScrn, int op,
                                              CARD16 red, CARD16 green,
                                              CARD16 blue, CARD16 alpha,
                                              CARD32 maskFormat,
                                              CARD32 dstFormat, CARD8 *alphaPtr,
                                              int alphaPitch, int width,
                                              int height, int flags);
    Bool (*SetupForCPUToScreenTexture2) (ScrnInfoPtr pScrn, int op,
                                         CARD32 srcFormat, CARD32 dstFormat,
                                         CARD8 *texPtr, int texPitch, int width,
                                         int height, int flags);
} XAAStateWrapRec, *XAAStateWrapPtr;

static DevPrivateKeyRec XAAStateKeyRec;

#define XAAStateKey (&XAAStateKeyRec)

/* Wrap functions start here */
#define GET_STATEPRIV_GC(pGC)   XAAStateWrapPtr pStatePriv =\
(XAAStateWrapPtr)dixLookupPrivate(&(pGC)->pScreen->devPrivates, XAAStateKey)

#define GET_STATEPRIV_SCREEN(pScreen)   XAAStateWrapPtr pStatePriv =\
(XAAStateWrapPtr)dixLookupPrivate(&(pScreen)->devPrivates, XAAStateKey)

#define GET_STATEPRIV_PSCRN(pScrn)   XAAStateWrapPtr pStatePriv =\
(XAAStateWrapPtr)dixLookupPrivate(&(pScrn)->pScreen->devPrivates, XAAStateKey)

#define STATE_CHECK_SP(pStatePriv) {\
	ScrnInfoPtr pScrn = pStatePriv->pScrn;\
	int i = 0;\
	int need_change = 0;\
	while(i < pScrn->numEntities) {\
		if(xf86IsEntityShared(pScrn->entityList[i]) &&\
		   xf86GetLastScrnFlag(pScrn->entityList[i]) != pScrn->scrnIndex) {\
			need_change = 1;\
			xf86SetLastScrnFlag(pScrn->entityList[i],\
					    pScrn->scrnIndex);\
		}\
		i++;\
	}\
	if(need_change == 1) (*pStatePriv->RestoreAccelState)(pScrn);\
}

#define STATE_CHECK_PSCRN(pScrn) {\
	int i = 0;\
	int need_change = 0;\
	while(i < pScrn->numEntities) {\
		if(xf86IsEntityShared(pScrn->entityList[i]) &&\
		   xf86GetLastScrnFlag(pScrn->entityList[i]) != pScrn->scrnIndex) {\
			need_change = 1;\
			xf86SetLastScrnFlag(pScrn->entityList[i],\
					    pScrn->scrnIndex);\
		}\
		i++;\
	}\
	if(need_change == 1) (*pStatePriv->RestoreAccelState)(pScrn);\
}

static void
XAAStateWrapSync(ScrnInfoPtr pScrn)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->Sync) (pScrn);
}

static void
XAAStateWrapSetupForScreenToScreenCopy(ScrnInfoPtr pScrn, int xdir, int ydir,
                                       int rop, unsigned int planemask,
                                       int trans_color)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForScreenToScreenCopy) (pScrn, xdir, ydir, rop,
                                               planemask, trans_color);
}

static void
XAAStateWrapSetupForSolidFill(ScrnInfoPtr pScrn, int color, int rop,
                              unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForSolidFill) (pScrn, color, rop, planemask);
}

static void
XAAStateWrapSetupForSolidLine(ScrnInfoPtr pScrn, int color, int rop,
                              unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForSolidLine) (pScrn, color, rop, planemask);
}

static void
XAAStateWrapSetupForDashedLine(ScrnInfoPtr pScrn, int fg, int bg, int rop,
                               unsigned int planemask, int length,
                               unsigned char *pattern)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForDashedLine) (pScrn, fg, bg, rop, planemask, length,
                                       pattern);
}

static void
XAAStateWrapSetClippingRectangle(ScrnInfoPtr pScrn, int left, int top,
                                 int right, int bottom)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetClippingRectangle) (pScrn, left, top, right, bottom);
}

static void
XAAStateWrapDisableClipping(ScrnInfoPtr pScrn)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->DisableClipping) (pScrn);
}

static void
XAAStateWrapSetupForMono8x8PatternFill(ScrnInfoPtr pScrn, int patx, int paty,
                                       int fg, int bg, int rop,
                                       unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForMono8x8PatternFill) (pScrn, patx, paty, fg, bg, rop,
                                               planemask);
}

static void
XAAStateWrapSetupForColor8x8PatternFill(ScrnInfoPtr pScrn, int patx, int paty,
                                        int rop, unsigned int planemask,
                                        int transparency_color)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForColor8x8PatternFill) (pScrn, patx, paty, rop,
                                                planemask, transparency_color);
}

static void
XAAStateWrapSetupForCPUToScreenColorExpandFill(ScrnInfoPtr pScrn, int fg,
                                               int bg, int rop,
                                               unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                       planemask);
}

static void
XAAStateWrapSetupForScanlineCPUToScreenColorExpandFill(ScrnInfoPtr pScrn,
                                                       int fg, int bg,
                                                       int rop,
                                                       unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg,
                                                               rop, planemask);
}

static void
XAAStateWrapSetupForScreenToScreenColorExpandFill(ScrnInfoPtr pScrn,
                                                  int fg, int bg, int rop,
                                                  unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForScreenToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                          planemask);
}

static void
XAAStateWrapSetupForImageWrite(ScrnInfoPtr pScrn, int rop,
                               unsigned int planemask, int transparency_color,
                               int bpp, int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForImageWrite) (pScrn, rop, planemask,
                                       transparency_color, bpp, depth);
}

static void
XAAStateWrapSetupForScanlineImageWrite(ScrnInfoPtr pScrn, int rop,
                                       unsigned int planemask,
                                       int transparency_color,
                                       int bpp, int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForScanlineImageWrite) (pScrn, rop, planemask,
                                               transparency_color, bpp, depth);
}

static void
XAAStateWrapSetupForImageRead(ScrnInfoPtr pScrn, int bpp, int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->SetupForImageRead) (pScrn, bpp, depth);
}

static void
XAAStateWrapScreenToScreenBitBlt(ScrnInfoPtr pScrn, int nbox,
                                 DDXPointPtr pptSrc, BoxPtr pbox, int xdir,
                                 int ydir, int alu, unsigned int planmask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->ScreenToScreenBitBlt) (pScrn, nbox,
                                         pptSrc, pbox, xdir,
                                         ydir, alu, planmask);
}

static void
XAAStateWrapWriteBitmap(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                        unsigned char *src, int srcwidth, int skipleft,
                        int fg, int bg, int rop, unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WriteBitmap) (pScrn, x, y, w, h,
                                src, srcwidth, skipleft,
                                fg, bg, rop, planemask);
}

static void
XAAStateWrapFillSolidRects(ScrnInfoPtr pScrn, int fg, int rop,
                           unsigned int planemask, int nBox, BoxPtr pBox)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillSolidRects) (pScrn, fg, rop, planemask, nBox, pBox);
}

static void
XAAStateWrapFillMono8x8PatternRects(ScrnInfoPtr pScrn, int fg, int bg,
                                    int rop, unsigned int planemask, int nBox,
                                    BoxPtr pBox, int pat0, int pat1,
                                    int xorg, int yorg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillMono8x8PatternRects) (pScrn, fg, bg,
                                            rop, planemask, nBox,
                                            pBox, pat0, pat1, xorg, yorg);
}

static void
XAAStateWrapFillColor8x8PatternRects(ScrnInfoPtr pScrn, int rop,
                                     unsigned int planemask, int nBox,
                                     BoxPtr pBox, int xorg, int yorg,
                                     XAACacheInfoPtr pCache)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillColor8x8PatternRects) (pScrn, rop,
                                             planemask, nBox,
                                             pBox, xorg, yorg, pCache);
}

static void
XAAStateWrapFillCacheBltRects(ScrnInfoPtr pScrn, int rop,
                              unsigned int planemask, int nBox, BoxPtr pBox,
                              int xorg, int yorg, XAACacheInfoPtr pCache)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillCacheBltRects) (pScrn, rop,
                                      planemask, nBox, pBox,
                                      xorg, yorg, pCache);
}

static void
XAAStateWrapFillColorExpandRects(ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                 unsigned int planemask, int nBox,
                                 BoxPtr pBox, int xorg, int yorg,
                                 PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillColorExpandRects) (pScrn, fg, bg, rop,
                                         planemask, nBox,
                                         pBox, xorg, yorg, pPix);
}

static void
XAAStateWrapFillCacheExpandRects(ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                 unsigned int planemask, int nBox,
                                 BoxPtr pBox, int xorg, int yorg,
                                 PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillCacheExpandRects) (pScrn, fg, bg, rop,
                                         planemask, nBox,
                                         pBox, xorg, yorg, pPix);
}

static void
XAAStateWrapFillImageWriteRects(ScrnInfoPtr pScrn, int rop,
                                unsigned int planemask, int nBox, BoxPtr pBox,
                                int xorg, int yorg, PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillImageWriteRects) (pScrn, rop,
                                        planemask, nBox, pBox,
                                        xorg, yorg, pPix);
}

static void
XAAStateWrapFillSolidSpans(ScrnInfoPtr pScrn, int fg, int rop,
                           unsigned int planemask, int n, DDXPointPtr points,
                           int *widths, int fSorted)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillSolidSpans) (pScrn, fg, rop,
                                   planemask, n, points, widths, fSorted);
}

static void
XAAStateWrapFillMono8x8PatternSpans(ScrnInfoPtr pScrn, int fg, int bg,
                                    int rop, unsigned int planemask, int n,
                                    DDXPointPtr points, int *widths,
                                    int fSorted, int pat0, int pat1,
                                    int xorg, int yorg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillMono8x8PatternSpans) (pScrn, fg, bg,
                                            rop, planemask, n,
                                            points, widths,
                                            fSorted, pat0, pat1, xorg, yorg);
}

static void
XAAStateWrapFillColor8x8PatternSpans(ScrnInfoPtr pScrn, int rop,
                                     unsigned int planemask, int n,
                                     DDXPointPtr points, int *widths,
                                     int fSorted, XAACacheInfoPtr pCache,
                                     int xorg, int yorg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillColor8x8PatternSpans) (pScrn, rop,
                                             planemask, n,
                                             points, widths,
                                             fSorted, pCache, xorg, yorg);
}

static void
XAAStateWrapFillCacheBltSpans(ScrnInfoPtr pScrn, int rop,
                              unsigned int planemask, int n,
                              DDXPointPtr points, int *widths,
                              int fSorted, XAACacheInfoPtr pCache,
                              int xorg, int yorg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillCacheBltSpans) (pScrn, rop,
                                      planemask, n,
                                      points, widths,
                                      fSorted, pCache, xorg, yorg);
}

static void
XAAStateWrapFillColorExpandSpans(ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                 unsigned int planemask, int n,
                                 DDXPointPtr points, int *widths, int fSorted,
                                 int xorg, int yorg, PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillColorExpandSpans) (pScrn, fg, bg, rop,
                                         planemask, n,
                                         points, widths, fSorted,
                                         xorg, yorg, pPix);
}

static void
XAAStateWrapFillCacheExpandSpans(ScrnInfoPtr pScrn, int fg, int bg, int rop,
                                 unsigned int planemask, int n,
                                 DDXPointPtr ppt, int *pwidth, int fSorted,
                                 int xorg, int yorg, PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->FillCacheExpandSpans) (pScrn, fg, bg, rop,
                                         planemask, n,
                                         ppt, pwidth, fSorted,
                                         xorg, yorg, pPix);
}

static void
XAAStateWrapTEGlyphRenderer(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                            int skipleft, int startline,
                            unsigned int **glyphs,
                            int glyphWidth, int fg, int bg, int rop,
                            unsigned planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->TEGlyphRenderer) (pScrn, x, y, w, h,
                                    skipleft, startline,
                                    glyphs, glyphWidth, fg, bg, rop, planemask);
}

static void
XAAStateWrapNonTEGlyphRenderer(ScrnInfoPtr pScrn, int x, int y, int n,
                               NonTEGlyphPtr glyphs, BoxPtr pbox,
                               int fg, int rop, unsigned int planemask)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->NonTEGlyphRenderer) (pScrn, x, y, n,
                                       glyphs, pbox, fg, rop, planemask);
}

static void
XAAStateWrapWritePixmap(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                        unsigned char *src, int srcwidth, int rop,
                        unsigned int planemask, int transparency_color,
                        int bpp, int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WritePixmap) (pScrn, x, y, w, h,
                                src, srcwidth, rop,
                                planemask, transparency_color, bpp, depth);
}

static void
XAAStateWrapReadPixmap(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                       unsigned char *dst, int dstwidth, int bpp, int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->ReadPixmap) (pScrn, x, y, w, h, dst, dstwidth, bpp, depth);
}

static RegionPtr
XAAStateWrapCopyArea(DrawablePtr pSrcDrawable, DrawablePtr pDstDrawable,
                     GC * pGC, int srcx, int srcy, int width, int height,
                     int dstx, int dsty)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->CopyArea) (pSrcDrawable, pDstDrawable,
                                    pGC, srcx, srcy, width, height, dstx, dsty);
}

static RegionPtr
XAAStateWrapCopyPlane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                      int srcx, int srcy, int width, int height,
                      int dstx, int dsty, unsigned long bitPlane)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->CopyPlane) (pSrc, pDst, pGC,
                                     srcx, srcy, width, height,
                                     dstx, dsty, bitPlane);
}

static void
XAAStateWrapPushPixelsSolid(GCPtr pGC, PixmapPtr pBitMap,
                            DrawablePtr pDrawable, int dx, int dy, int xOrg,
                            int yOrg)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PushPixelsSolid) (pGC, pBitMap,
                                    pDrawable, dx, dy, xOrg, yOrg);
}

static void
XAAStateWrapPolyFillRectSolid(DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                              xRectangle *prectInit)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyFillRectSolid) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAStateWrapPolyFillRectStippled(DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                                 xRectangle *prectInit)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyFillRectStippled) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAStateWrapPolyFillRectOpaqueStippled(DrawablePtr pDraw, GCPtr pGC,
                                       int nrectFill, xRectangle *prectInit)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyFillRectOpaqueStippled) (pDraw, pGC,
                                               nrectFill, prectInit);
}

static void
XAAStateWrapPolyFillRectTiled(DrawablePtr pDraw, GCPtr pGC, int nrectFill,
                              xRectangle *prectInit)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyFillRectTiled) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAStateWrapFillSpansSolid(DrawablePtr pDraw, GCPtr pGC, int nInit,
                           DDXPointPtr ppt, int *pwidth, int fSorted)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillSpansSolid) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static void
XAAStateWrapFillSpansStippled(DrawablePtr pDraw, GCPtr pGC, int nInit,
                              DDXPointPtr ppt, int *pwidth, int fSorted)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillSpansStippled) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static void
XAAStateWrapFillSpansOpaqueStippled(DrawablePtr pDraw, GCPtr pGC, int nInit,
                                    DDXPointPtr ppt, int *pwidth, int fSorted)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillSpansOpaqueStippled) (pDraw, pGC, nInit,
                                            ppt, pwidth, fSorted);
}

static void
XAAStateWrapFillSpansTiled(DrawablePtr pDraw, GCPtr pGC, int nInit,
                           DDXPointPtr ppt, int *pwidth, int fSorted)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillSpansTiled) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static int
XAAStateWrapPolyText8TE(DrawablePtr pDraw, GCPtr pGC, int x, int y, int count,
                        char *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->PolyText8TE) (pDraw, pGC, x, y, count, chars);
}

static int
XAAStateWrapPolyText16TE(DrawablePtr pDraw, GCPtr pGC, int x, int y, int count,
                         unsigned short *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->PolyText16TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageText8TE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                         int count, char *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageText8TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageText16TE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                          int count, unsigned short *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageText16TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageGlyphBltTE(DrawablePtr pDrawable, GCPtr pGC, int xInit,
                            int yInit, unsigned int nglyph, CharInfoPtr * ppci,
                            pointer pglyphBase)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageGlyphBltTE) (pDrawable, pGC, xInit,
                                    yInit, nglyph, ppci, pglyphBase);
}

static void
XAAStateWrapPolyGlyphBltTE(DrawablePtr pDrawable, GCPtr pGC, int xInit,
                           int yInit, unsigned int nglyph, CharInfoPtr * ppci,
                           pointer pglyphBase)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyGlyphBltTE) (pDrawable, pGC, xInit,
                                   yInit, nglyph, ppci, pglyphBase);
}

static int
XAAStateWrapPolyText8NonTE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                           int count, char *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->PolyText8NonTE) (pDraw, pGC, x, y, count, chars);
}

static int
XAAStateWrapPolyText16NonTE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                            int count, unsigned short *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->PolyText16NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageText8NonTE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                            int count, char *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageText8NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageText16NonTE(DrawablePtr pDraw, GCPtr pGC, int x, int y,
                             int count, unsigned short *chars)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageText16NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAStateWrapImageGlyphBltNonTE(DrawablePtr pDrawable, GCPtr pGC, int xInit,
                               int yInit, unsigned int nglyph,
                               CharInfoPtr * ppci, pointer pglyphBase)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ImageGlyphBltNonTE) (pDrawable, pGC, xInit,
                                       yInit, nglyph, ppci, pglyphBase);
}

static void
XAAStateWrapPolyGlyphBltNonTE(DrawablePtr pDrawable, GCPtr pGC, int xInit,
                              int yInit, unsigned int nglyph,
                              CharInfoPtr * ppci, pointer pglyphBase)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyGlyphBltNonTE) (pDrawable, pGC, xInit,
                                      yInit, nglyph, ppci, pglyphBase);
}

static void
XAAStateWrapPolyRectangleThinSolid(DrawablePtr pDrawable, GCPtr pGC,
                                   int nRectsInit, xRectangle *pRectsInit)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyRectangleThinSolid) (pDrawable, pGC,
                                           nRectsInit, pRectsInit);
}

static void
XAAStateWrapPolylinesWideSolid(DrawablePtr pDrawable, GCPtr pGC, int mode,
                               int npt, DDXPointPtr pPts)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolylinesWideSolid) (pDrawable, pGC, mode, npt, pPts);
}

static void
XAAStateWrapPolylinesThinSolid(DrawablePtr pDrawable, GCPtr pGC, int mode,
                               int npt, DDXPointPtr pPts)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolylinesThinSolid) (pDrawable, pGC, mode, npt, pPts);
}

static void
XAAStateWrapPolySegmentThinSolid(DrawablePtr pDrawable, GCPtr pGC, int nseg,
                                 xSegment * pSeg)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolySegmentThinSolid) (pDrawable, pGC, nseg, pSeg);
}

static void
XAAStateWrapPolylinesThinDashed(DrawablePtr pDrawable, GCPtr pGC, int mode,
                                int npt, DDXPointPtr pPts)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolylinesThinDashed) (pDrawable, pGC, mode, npt, pPts);
}

static void
XAAStateWrapPolySegmentThinDashed(DrawablePtr pDrawable, GCPtr pGC, int nseg,
                                  xSegment * pSeg)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolySegmentThinDashed) (pDrawable, pGC, nseg, pSeg);
}

static void
XAAStateWrapFillPolygonSolid(DrawablePtr pDrawable, GCPtr pGC, int shape,
                             int mode, int count, DDXPointPtr ptsIn)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillPolygonSolid) (pDrawable, pGC, shape, mode, count, ptsIn);
}

static void
XAAStateWrapFillPolygonStippled(DrawablePtr pDrawable, GCPtr pGC, int shape,
                                int mode, int count, DDXPointPtr ptsIn)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillPolygonStippled) (pDrawable, pGC, shape,
                                        mode, count, ptsIn);
}

static void
XAAStateWrapFillPolygonOpaqueStippled(DrawablePtr pDrawable, GCPtr pGC,
                                      int shape, int mode, int count,
                                      DDXPointPtr ptsIn)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillPolygonOpaqueStippled) (pDrawable, pGC,
                                              shape, mode, count, ptsIn);
}

static void
XAAStateWrapFillPolygonTiled(DrawablePtr pDrawable, GCPtr pGC, int shape,
                             int mode, int count, DDXPointPtr ptsIn)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->FillPolygonTiled) (pDrawable, pGC, shape, mode, count, ptsIn);
}

static void
XAAStateWrapPolyFillArcSolid(DrawablePtr pDraw, GCPtr pGC, int narcs,
                             xArc * parcs)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PolyFillArcSolid) (pDraw, pGC, narcs, parcs);
}

static void
XAAStateWrapPutImage(DrawablePtr pDraw, GCPtr pGC, int depth, int x, int y,
                     int w, int h, int leftPad, int format, char *pImage)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->PutImage) (pDraw, pGC, depth, x, y,
                             w, h, leftPad, format, pImage);
}

static void
XAAStateWrapValidateFillSpans(GCPtr pGC, unsigned long changes,
                              DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateFillSpans) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateSetSpans(GCPtr pGC, unsigned long changes,
                             DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateSetSpans) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePutImage(GCPtr pGC, unsigned long changes,
                             DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePutImage) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateCopyArea(GCPtr pGC, unsigned long changes,
                             DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateCopyArea) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateCopyPlane(GCPtr pGC, unsigned long changes,
                              DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateCopyPlane) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyPoint(GCPtr pGC, unsigned long changes,
                              DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyPoint) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolylines(GCPtr pGC, unsigned long changes,
                              DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolylines) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolySegment(GCPtr pGC, unsigned long changes,
                                DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolySegment) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyRectangle(GCPtr pGC, unsigned long changes,
                                  DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyRectangle) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyArc(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyArc) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateFillPolygon(GCPtr pGC, unsigned long changes,
                                DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateFillPolygon) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyFillRect(GCPtr pGC, unsigned long changes,
                                 DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyFillRect) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyFillArc(GCPtr pGC, unsigned long changes,
                                DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyFillArc) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyText8(GCPtr pGC, unsigned long changes,
                              DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyText8) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyText16(GCPtr pGC, unsigned long changes,
                               DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyText16) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateImageText8(GCPtr pGC, unsigned long changes,
                               DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateImageText8) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateImageText16(GCPtr pGC, unsigned long changes,
                                DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidateImageText16) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePolyGlyphBlt(GCPtr pGC, unsigned long changes,
                                 DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePolyGlyphBlt) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidateImageGlyphBlt(GCPtr pGC, unsigned long changes,
                                  DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);
    (*pStatePriv->ValidateImageGlyphBlt) (pGC, changes, pDraw);
}

static void
XAAStateWrapValidatePushPixels(GCPtr pGC, unsigned long changes,
                               DrawablePtr pDraw)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ValidatePushPixels) (pGC, changes, pDraw);
}

static void
XAAStateWrapComputeDash(GCPtr pGC)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ComputeDash) (pGC);
}

static void
XAAStateWrapInitPixmapCache(ScreenPtr pScreen, RegionPtr areas, pointer data)
{
    GET_STATEPRIV_SCREEN(pScreen);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->InitPixmapCache) (pScreen, areas, data);
}

static void
XAAStateWrapClosePixmapCache(ScreenPtr pScreen)
{
    GET_STATEPRIV_SCREEN(pScreen);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->ClosePixmapCache) (pScreen);
}

static int
XAAStateWrapStippledFillChooser(GCPtr pGC)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->StippledFillChooser) (pGC);
}

static int
XAAStateWrapOpaqueStippledFillChooser(GCPtr pGC)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->OpaqueStippledFillChooser) (pGC);
}

static int
XAAStateWrapTiledFillChooser(GCPtr pGC)
{
    GET_STATEPRIV_GC(pGC);
    STATE_CHECK_SP(pStatePriv);

    return (*pStatePriv->TiledFillChooser) (pGC);
}

static XAACacheInfoPtr
XAAStateWrapCacheTile(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->CacheTile) (pScrn, pPix);
}

static XAACacheInfoPtr
XAAStateWrapCacheStipple(ScrnInfoPtr pScrn, PixmapPtr pPix, int fg, int bg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->CacheStipple) (pScrn, pPix, fg, bg);
}

static XAACacheInfoPtr
XAAStateWrapCacheMonoStipple(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->CacheMonoStipple) (pScrn, pPix);
}

static XAACacheInfoPtr
XAAStateWrapCacheMono8x8Pattern(ScrnInfoPtr pScrn, int pat0, int pat1)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->CacheMono8x8Pattern) (pScrn, pat0, pat1);
}

static XAACacheInfoPtr
XAAStateWrapCacheColor8x8Pattern(ScrnInfoPtr pScrn, PixmapPtr pPix,
                                 int fg, int bg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->CacheColor8x8Pattern) (pScrn, pPix, fg, bg);
}

static void
XAAStateWrapWriteBitmapToCache(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                               unsigned char *src, int srcwidth, int fg, int bg)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WriteBitmapToCache) (pScrn, x, y, w, h,
                                       src, srcwidth, fg, bg);
}

static void
XAAStateWrapWritePixmapToCache(ScrnInfoPtr pScrn, int x, int y, int w, int h,
                               unsigned char *src, int srcwidth, int bpp,
                               int depth)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WritePixmapToCache) (pScrn, x, y, w, h,
                                       src, srcwidth, bpp, depth);
}

static void
XAAStateWrapWriteMono8x8PatternToCache(ScrnInfoPtr pScrn,
                                       XAACacheInfoPtr pCache)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WriteMono8x8PatternToCache) (pScrn, pCache);
}

static void
XAAStateWrapWriteColor8x8PatternToCache(ScrnInfoPtr pScrn, PixmapPtr pPix,
                                        XAACacheInfoPtr pCache)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    (*pStatePriv->WriteColor8x8PatternToCache) (pScrn, pPix, pCache);
}

static void
XAAStateWrapGetImage(DrawablePtr pDrawable, int sx, int sy, int w, int h,
                     unsigned int format, unsigned long planeMask,
                     char *pdstLine)
{
    GET_STATEPRIV_SCREEN(pDrawable->pScreen);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->GetImage) (pDrawable, sx, sy, w, h,
                             format, planeMask, pdstLine);
}

static void
XAAStateWrapGetSpans(DrawablePtr pDrawable, int wMax, DDXPointPtr ppt,
                     int *pwidth, int nspans, char *pdstStart)
{
    GET_STATEPRIV_SCREEN(pDrawable->pScreen);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->GetSpans) (pDrawable, wMax, ppt, pwidth, nspans, pdstStart);
}

static void
XAAStateWrapCopyWindow(WindowPtr pWindow, DDXPointRec ptOldOrg,
                       RegionPtr prgnSrc)
{
    GET_STATEPRIV_SCREEN(pWindow->drawable.pScreen);
    STATE_CHECK_SP(pStatePriv);

    (*pStatePriv->CopyWindow) (pWindow, ptOldOrg, prgnSrc);
}

static Bool
XAAStateWrapSetupForCPUToScreenAlphaTexture2(ScrnInfoPtr pScrn,
                                             int op, CARD16 red,
                                             CARD16 green,
                                             CARD16 blue,
                                             CARD16 alpha,
                                             CARD32 srcFormat,
                                             CARD32 dstFormat,
                                             CARD8 *alphaPtr,
                                             int alphaPitch,
                                             int width, int height, int flags)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->SetupForCPUToScreenAlphaTexture2) (pScrn, op, red,
                                                            green, blue, alpha,
                                                            srcFormat,
                                                            dstFormat, alphaPtr,
                                                            alphaPitch, width,
                                                            height, flags);
}

static Bool
XAAStateWrapSetupForCPUToScreenTexture2(ScrnInfoPtr pScrn, int op,
                                        CARD32 srcFormat,
                                        CARD32 dstFormat,
                                        CARD8 *texPtr, int texPitch,
                                        int width, int height, int flags)
{
    GET_STATEPRIV_PSCRN(pScrn);
    STATE_CHECK_PSCRN(pScrn);

    return (*pStatePriv->SetupForCPUToScreenTexture2) (pScrn, op, srcFormat,
                                                       dstFormat, texPtr,
                                                       texPitch, width, height,
                                                       flags);
}

/* Setup Function */
Bool
XAAInitStateWrap(ScreenPtr pScreen, XAAInfoRecPtr infoRec)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAStateWrapPtr pStatePriv;
    int i = 0;

    if (!dixRegisterPrivateKey(&XAAStateKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;
    if (!(pStatePriv = malloc(sizeof(XAAStateWrapRec))))
        return FALSE;
    dixSetPrivate(&pScreen->devPrivates, XAAStateKey, pStatePriv);
    pStatePriv->RestoreAccelState = infoRec->RestoreAccelState;
    pStatePriv->pScrn = pScrn;

    /* Initialize the last screen to -1 so whenever an accel function
     * is called the proper state is setup
     */
    while (i < pScrn->numEntities) {
        xf86SetLastScrnFlag(pScrn->entityList[i], -1);
        i++;
    }
/* Do the wrapping */
    XAA_STATE_WRAP(Sync);
    XAA_STATE_WRAP(SetupForScreenToScreenCopy);
    XAA_STATE_WRAP(SetupForSolidFill);
    XAA_STATE_WRAP(SetupForSolidLine);
    XAA_STATE_WRAP(SetupForDashedLine);
    XAA_STATE_WRAP(SetClippingRectangle);
    XAA_STATE_WRAP(DisableClipping);
    XAA_STATE_WRAP(SetupForMono8x8PatternFill);
    XAA_STATE_WRAP(SetupForColor8x8PatternFill);
    XAA_STATE_WRAP(SetupForCPUToScreenColorExpandFill);
    XAA_STATE_WRAP(SetupForScanlineCPUToScreenColorExpandFill);
    XAA_STATE_WRAP(SetupForScreenToScreenColorExpandFill);
    XAA_STATE_WRAP(SetupForImageWrite);
    XAA_STATE_WRAP(SetupForScanlineImageWrite);
    XAA_STATE_WRAP(SetupForImageRead);
    XAA_STATE_WRAP(ScreenToScreenBitBlt);
    XAA_STATE_WRAP(WriteBitmap);
    XAA_STATE_WRAP(FillSolidRects);
    XAA_STATE_WRAP(FillMono8x8PatternRects);
    XAA_STATE_WRAP(FillColor8x8PatternRects);
    XAA_STATE_WRAP(FillCacheBltRects);
    XAA_STATE_WRAP(FillColorExpandRects);
    XAA_STATE_WRAP(FillCacheExpandRects);
    XAA_STATE_WRAP(FillImageWriteRects);
    XAA_STATE_WRAP(FillSolidSpans);
    XAA_STATE_WRAP(FillMono8x8PatternSpans);
    XAA_STATE_WRAP(FillColor8x8PatternSpans);
    XAA_STATE_WRAP(FillCacheBltSpans);
    XAA_STATE_WRAP(FillColorExpandSpans);
    XAA_STATE_WRAP(FillCacheExpandSpans);
    XAA_STATE_WRAP(TEGlyphRenderer);
    XAA_STATE_WRAP(NonTEGlyphRenderer);
    XAA_STATE_WRAP(WritePixmap);
    XAA_STATE_WRAP(ReadPixmap);
    XAA_STATE_WRAP(CopyArea);
    XAA_STATE_WRAP(CopyPlane);
    XAA_STATE_WRAP(PushPixelsSolid);
    XAA_STATE_WRAP(PolyFillRectSolid);
    XAA_STATE_WRAP(PolyFillRectStippled);
    XAA_STATE_WRAP(PolyFillRectOpaqueStippled);
    XAA_STATE_WRAP(PolyFillRectTiled);
    XAA_STATE_WRAP(FillSpansSolid);
    XAA_STATE_WRAP(FillSpansStippled);
    XAA_STATE_WRAP(FillSpansOpaqueStippled);
    XAA_STATE_WRAP(FillSpansTiled);
    XAA_STATE_WRAP(PolyText8TE);
    XAA_STATE_WRAP(PolyText16TE);
    XAA_STATE_WRAP(ImageText8TE);
    XAA_STATE_WRAP(ImageText16TE);
    XAA_STATE_WRAP(ImageGlyphBltTE);
    XAA_STATE_WRAP(PolyGlyphBltTE);
    XAA_STATE_WRAP(PolyText8NonTE);
    XAA_STATE_WRAP(PolyText16NonTE);
    XAA_STATE_WRAP(ImageText8NonTE);
    XAA_STATE_WRAP(ImageText16NonTE);
    XAA_STATE_WRAP(ImageGlyphBltNonTE);
    XAA_STATE_WRAP(PolyGlyphBltNonTE);
    XAA_STATE_WRAP(PolyRectangleThinSolid);
    XAA_STATE_WRAP(PolylinesWideSolid);
    XAA_STATE_WRAP(PolylinesThinSolid);
    XAA_STATE_WRAP(PolySegmentThinSolid);
    XAA_STATE_WRAP(PolylinesThinDashed);
    XAA_STATE_WRAP(PolySegmentThinDashed);
    XAA_STATE_WRAP(FillPolygonSolid);
    XAA_STATE_WRAP(FillPolygonStippled);
    XAA_STATE_WRAP(FillPolygonOpaqueStippled);
    XAA_STATE_WRAP(FillPolygonTiled);
    XAA_STATE_WRAP(PolyFillArcSolid);
    XAA_STATE_WRAP(PutImage);
    XAA_STATE_WRAP(ValidateFillSpans);
    XAA_STATE_WRAP(ValidateSetSpans);
    XAA_STATE_WRAP(ValidatePutImage);
    XAA_STATE_WRAP(ValidateCopyArea);
    XAA_STATE_WRAP(ValidateCopyPlane);
    XAA_STATE_WRAP(ValidatePolyPoint);
    XAA_STATE_WRAP(ValidatePolylines);
    XAA_STATE_WRAP(ValidatePolySegment);
    XAA_STATE_WRAP(ValidatePolyRectangle);
    XAA_STATE_WRAP(ValidatePolyArc);
    XAA_STATE_WRAP(ValidateFillPolygon);
    XAA_STATE_WRAP(ValidatePolyFillRect);
    XAA_STATE_WRAP(ValidatePolyFillArc);
    XAA_STATE_WRAP(ValidatePolyText8);
    XAA_STATE_WRAP(ValidatePolyText16);
    XAA_STATE_WRAP(ValidateImageText8);
    XAA_STATE_WRAP(ValidateImageText16);
    XAA_STATE_WRAP(ValidatePolyGlyphBlt);
    XAA_STATE_WRAP(ValidateImageGlyphBlt);
    XAA_STATE_WRAP(ValidatePushPixels);
    XAA_STATE_WRAP(ComputeDash);
    XAA_STATE_WRAP(InitPixmapCache);
    XAA_STATE_WRAP(ClosePixmapCache);
    XAA_STATE_WRAP(StippledFillChooser);
    XAA_STATE_WRAP(OpaqueStippledFillChooser);
    XAA_STATE_WRAP(TiledFillChooser);
    XAA_STATE_WRAP(CacheTile);
    XAA_STATE_WRAP(CacheStipple);
    XAA_STATE_WRAP(CacheMonoStipple);
    XAA_STATE_WRAP(CacheMono8x8Pattern);
    XAA_STATE_WRAP(CacheColor8x8Pattern);
    XAA_STATE_WRAP(WriteBitmapToCache);
    XAA_STATE_WRAP(WritePixmapToCache);
    XAA_STATE_WRAP(WriteMono8x8PatternToCache);
    XAA_STATE_WRAP(WriteColor8x8PatternToCache);
    XAA_STATE_WRAP(GetImage);
    XAA_STATE_WRAP(GetSpans);
    XAA_STATE_WRAP(CopyWindow);
    XAA_STATE_WRAP(SetupForCPUToScreenAlphaTexture2);
    XAA_STATE_WRAP(SetupForCPUToScreenTexture2);
    return TRUE;
}
