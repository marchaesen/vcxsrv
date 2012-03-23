
#ifndef _XAALOCAL_H
#define _XAALOCAL_H

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

/* This file is very unorganized ! */

#include "gcstruct.h"
#include "regionstr.h"
#include "xf86fbman.h"
#include "xaa.h"
#include "mi.h"
#include "picturestr.h"

#define GCWhenForced		(GCArcMode << 1)

#define DO_COLOR_8x8		0x00000001
#define DO_MONO_8x8		0x00000002
#define DO_CACHE_BLT		0x00000003
#define DO_COLOR_EXPAND		0x00000004
#define DO_CACHE_EXPAND		0x00000005
#define DO_IMAGE_WRITE		0x00000006
#define DO_PIXMAP_COPY		0x00000007
#define DO_SOLID		0x00000008

typedef CARD32 *(*GlyphScanlineFuncPtr) (CARD32 *base, unsigned int **glyphp,
                                         int line, int nglyph, int width);

typedef CARD32 *(*StippleScanlineProcPtr) (CARD32 *, CARD32 *, int, int, int);

typedef void (*RectFuncPtr) (ScrnInfoPtr, int, int, int, int, int, int,
                             XAACacheInfoPtr);
typedef void (*TrapFuncPtr) (ScrnInfoPtr, int, int, int, int, int, int,
                             int, int, int, int, int, int, XAACacheInfoPtr);

typedef struct _XAAScreen {
    CreateGCProcPtr CreateGC;
    CloseScreenProcPtr CloseScreen;
    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;
    CopyWindowProcPtr CopyWindow;
    WindowExposuresProcPtr WindowExposures;
    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;
    ChangeWindowAttributesProcPtr ChangeWindowAttributes;
    XAAInfoRecPtr AccelInfoRec;
    Bool (*EnterVT) (int, int);
    void (*LeaveVT) (int, int);
    int (*SetDGAMode) (int, int, DGADevicePtr);
    void (*EnableDisableFBAccess) (int, Bool);
    CompositeProcPtr Composite;
    GlyphsProcPtr Glyphs;
} XAAScreenRec, *XAAScreenPtr;

#define	OPS_ARE_PIXMAP		0x00000001
#define OPS_ARE_ACCEL		0x00000002

typedef struct _XAAGC {
    GCOps *wrapOps;
    GCFuncs *wrapFuncs;
    GCOps *XAAOps;
    int DashLength;
    unsigned char *DashPattern;
    unsigned long changes;
    unsigned long flags;
} XAAGCRec, *XAAGCPtr;

#define REDUCIBILITY_CHECKED	0x00000001
#define REDUCIBLE_TO_8x8	0x00000002
#define REDUCIBLE_TO_2_COLOR	0x00000004
#define DIRTY			0x00010000
#define OFFSCREEN		0x00020000
#define DGA_PIXMAP		0x00040000
#define SHARED_PIXMAP		0x00080000
#define LOCKED_PIXMAP		0x00100000

#define REDUCIBILITY_MASK \
 (REDUCIBILITY_CHECKED | REDUCIBLE_TO_8x8 | REDUCIBLE_TO_2_COLOR)

typedef struct _XAAPixmap {
    unsigned long flags;
    CARD32 pattern0;
    CARD32 pattern1;
    int fg;
    int bg;
    FBAreaPtr offscreenArea;
    Bool freeData;
} XAAPixmapRec, *XAAPixmapPtr;

extern _X_EXPORT Bool
 XAACreateGC(GCPtr pGC);

extern _X_EXPORT Bool
 XAAInitAccel(ScreenPtr pScreen, XAAInfoRecPtr infoRec);

extern _X_EXPORT RegionPtr

XAABitBlt(DrawablePtr pSrcDrawable,
          DrawablePtr pDstDrawable,
          GC * pGC,
          int srcx,
          int srcy,
          int width,
          int height,
          int dstx,
          int dsty,
          void (*doBitBlt) (DrawablePtr, DrawablePtr, GCPtr, RegionPtr,
                            DDXPointPtr), unsigned long bitPlane);

extern _X_EXPORT void

XAAScreenToScreenBitBlt(ScrnInfoPtr pScrn,
                        int nbox,
                        DDXPointPtr pptSrc,
                        BoxPtr pbox,
                        int xdir, int ydir, int alu, unsigned int planemask);

extern _X_EXPORT void

XAADoBitBlt(DrawablePtr pSrc,
            DrawablePtr pDst, GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc);

extern _X_EXPORT void

XAADoImageWrite(DrawablePtr pSrc,
                DrawablePtr pDst,
                GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc);

extern _X_EXPORT void

XAADoImageRead(DrawablePtr pSrc,
               DrawablePtr pDst,
               GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc);

extern _X_EXPORT void
 XAACopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc);

extern _X_EXPORT RegionPtr

XAACopyArea(DrawablePtr pSrcDrawable,
            DrawablePtr pDstDrawable,
            GC * pGC,
            int srcx, int srcy, int width, int height, int dstx, int dsty);

extern _X_EXPORT void
 XAAValidateCopyArea(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidatePutImage(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidateCopyPlane(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidatePushPixels(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidateFillSpans(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidatePolyGlyphBlt(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidateImageGlyphBlt(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT void
 XAAValidatePolylines(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);

extern _X_EXPORT RegionPtr

XAACopyPlaneColorExpansion(DrawablePtr pSrc,
                           DrawablePtr pDst,
                           GCPtr pGC,
                           int srcx,
                           int srcy,
                           int width,
                           int height,
                           int dstx, int dsty, unsigned long bitPlane);

extern _X_EXPORT void

XAAPushPixelsSolidColorExpansion(GCPtr pGC,
                                 PixmapPtr pBitMap,
                                 DrawablePtr pDrawable,
                                 int dx, int dy, int xOrg, int yOrg);

extern _X_EXPORT void

XAAWriteBitmapColorExpandMSBFirstFixedBase(ScrnInfoPtr pScrn,
                                           int x, int y, int w, int h,
                                           unsigned char *src,
                                           int srcwidth,
                                           int skipleft,
                                           int fg, int bg,
                                           int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpand3MSBFirstFixedBase(ScrnInfoPtr pScrn,
                                            int x, int y, int w, int h,
                                            unsigned char *src,
                                            int srcwidth,
                                            int skipleft,
                                            int fg, int bg,
                                            int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpandMSBFirst(ScrnInfoPtr pScrn,
                                  int x, int y, int w, int h,
                                  unsigned char *src,
                                  int srcwidth,
                                  int skipleft,
                                  int fg, int bg,
                                  int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpand3MSBFirst(ScrnInfoPtr pScrn,
                                   int x, int y, int w, int h,
                                   unsigned char *src,
                                   int srcwidth,
                                   int skipleft,
                                   int fg, int bg,
                                   int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpandLSBFirstFixedBase(ScrnInfoPtr pScrn,
                                           int x, int y, int w, int h,
                                           unsigned char *src,
                                           int srcwidth,
                                           int skipleft,
                                           int fg, int bg,
                                           int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpand3LSBFirstFixedBase(ScrnInfoPtr pScrn,
                                            int x, int y, int w, int h,
                                            unsigned char *src,
                                            int srcwidth,
                                            int skipleft,
                                            int fg, int bg,
                                            int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpandLSBFirst(ScrnInfoPtr pScrn,
                                  int x, int y, int w, int h,
                                  unsigned char *src,
                                  int srcwidth,
                                  int skipleft,
                                  int fg, int bg,
                                  int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapColorExpand3LSBFirst(ScrnInfoPtr pScrn,
                                   int x, int y, int w, int h,
                                   unsigned char *src,
                                   int srcwidth,
                                   int skipleft,
                                   int fg, int bg,
                                   int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpandMSBFirst(ScrnInfoPtr pScrn,
                                          int x, int y, int w, int h,
                                          unsigned char *src,
                                          int srcwidth,
                                          int skipleft,
                                          int fg, int bg,
                                          int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpand3MSBFirst(ScrnInfoPtr pScrn,
                                           int x, int y, int w, int h,
                                           unsigned char *src,
                                           int srcwidth,
                                           int skipleft,
                                           int fg, int bg,
                                           int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpandMSBFirstFixedBase(ScrnInfoPtr pScrn,
                                                   int x, int y, int w, int h,
                                                   unsigned char *src,
                                                   int srcwidth,
                                                   int skipleft,
                                                   int fg, int bg,
                                                   int rop,
                                                   unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpand3MSBFirstFixedBase(ScrnInfoPtr pScrn,
                                                    int x, int y, int w, int h,
                                                    unsigned char *src,
                                                    int srcwidth,
                                                    int skipleft,
                                                    int fg, int bg,
                                                    int rop,
                                                    unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpandLSBFirst(ScrnInfoPtr pScrn,
                                          int x, int y, int w, int h,
                                          unsigned char *src,
                                          int srcwidth,
                                          int skipleft,
                                          int fg, int bg,
                                          int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpand3LSBFirst(ScrnInfoPtr pScrn,
                                           int x, int y, int w, int h,
                                           unsigned char *src,
                                           int srcwidth,
                                           int skipleft,
                                           int fg, int bg,
                                           int rop, unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpandLSBFirstFixedBase(ScrnInfoPtr pScrn,
                                                   int x, int y, int w, int h,
                                                   unsigned char *src,
                                                   int srcwidth,
                                                   int skipleft,
                                                   int fg, int bg,
                                                   int rop,
                                                   unsigned int planemask);

extern _X_EXPORT void

XAAWriteBitmapScanlineColorExpand3LSBFirstFixedBase(ScrnInfoPtr pScrn,
                                                    int x, int y, int w, int h,
                                                    unsigned char *src,
                                                    int srcwidth,
                                                    int skipleft,
                                                    int fg, int bg,
                                                    int rop,
                                                    unsigned int planemask);

extern _X_EXPORT void

XAAWritePixmap(ScrnInfoPtr pScrn,
               int x, int y, int w, int h,
               unsigned char *src,
               int srcwidth,
               int rop,
               unsigned int planemask,
               int transparency_color, int bpp, int depth);

extern _X_EXPORT void

XAAWritePixmapScanline(ScrnInfoPtr pScrn,
                       int x, int y, int w, int h,
                       unsigned char *src,
                       int srcwidth,
                       int rop,
                       unsigned int planemask,
                       int transparency_color, int bpp, int depth);

typedef void (*ClipAndRenderRectsFunc) (GCPtr, int, BoxPtr, int, int);

extern _X_EXPORT void

XAAClipAndRenderRects(GCPtr pGC,
                      ClipAndRenderRectsFunc func,
                      int nrectFill, xRectangle *prectInit, int xorg, int yorg);

typedef void (*ClipAndRenderSpansFunc) (GCPtr, int, DDXPointPtr, int *,
                                        int, int, int);

extern _X_EXPORT void

XAAClipAndRenderSpans(GCPtr pGC,
                      DDXPointPtr ppt,
                      int *pwidth,
                      int nspans,
                      int fSorted,
                      ClipAndRenderSpansFunc func, int xorg, int yorg);

extern _X_EXPORT void

XAAFillSolidRects(ScrnInfoPtr pScrn,
                  int fg, int rop,
                  unsigned int planemask, int nBox, BoxPtr pBox);

extern _X_EXPORT void

XAAFillMono8x8PatternRects(ScrnInfoPtr pScrn,
                           int fg, int bg, int rop,
                           unsigned int planemask,
                           int nBox,
                           BoxPtr pBox, int pat0, int pat1, int xorg, int yorg);

extern _X_EXPORT void

XAAFillMono8x8PatternRectsScreenOrigin(ScrnInfoPtr pScrn,
                                       int fg, int bg, int rop,
                                       unsigned int planemask,
                                       int nBox,
                                       BoxPtr pBox,
                                       int pat0, int pat1, int xorg, int yorg);

extern _X_EXPORT void

XAAFillColor8x8PatternRectsScreenOrigin(ScrnInfoPtr pScrn,
                                        int rop,
                                        unsigned int planemask,
                                        int nBox,
                                        BoxPtr pBox,
                                        int xorigin, int yorigin,
                                        XAACacheInfoPtr pCache);

extern _X_EXPORT void

XAAFillColor8x8PatternRects(ScrnInfoPtr pScrn,
                            int rop,
                            unsigned int planemask,
                            int nBox,
                            BoxPtr pBox,
                            int xorigin, int yorigin, XAACacheInfoPtr pCache);

extern _X_EXPORT void

XAAFillCacheBltRects(ScrnInfoPtr pScrn,
                     int rop,
                     unsigned int planemask,
                     int nBox,
                     BoxPtr pBox, int xorg, int yorg, XAACacheInfoPtr pCache);

extern _X_EXPORT void

XAAFillCacheExpandRects(ScrnInfoPtr pScrn,
                        int fg, int bg, int rop,
                        unsigned int planemask,
                        int nBox,
                        BoxPtr pBox, int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillImageWriteRects(ScrnInfoPtr pScrn,
                       int rop,
                       unsigned int planemask,
                       int nBox,
                       BoxPtr pBox, int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAPolyFillRect(DrawablePtr pDraw,
                GCPtr pGC, int nrectFill, xRectangle *prectInit);

extern _X_EXPORT void

XAATEGlyphRendererMSBFirstFixedBase(ScrnInfoPtr pScrn,
                                    int x, int y, int w, int h, int skipleft,
                                    int startline, unsigned int **glyphs,
                                    int glyphWidth, int fg, int bg, int rop,
                                    unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRenderer3MSBFirstFixedBase(ScrnInfoPtr pScrn,
                                     int x, int y, int w, int h, int skipleft,
                                     int startline, unsigned int **glyphs,
                                     int glyphWidth, int fg, int bg, int rop,
                                     unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererMSBFirst(ScrnInfoPtr pScrn,
                           int x, int y, int w, int h, int skipleft,
                           int startline, unsigned int **glyphs, int glyphWidth,
                           int fg, int bg, int rop, unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRenderer3MSBFirst(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h, int skipleft,
                            int startline, unsigned int **glyphs,
                            int glyphWidth, int fg, int bg, int rop,
                            unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererLSBFirstFixedBase(ScrnInfoPtr pScrn,
                                    int x, int y, int w, int h, int skipleft,
                                    int startline, unsigned int **glyphs,
                                    int glyphWidth, int fg, int bg, int rop,
                                    unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRenderer3LSBFirstFixedBase(ScrnInfoPtr pScrn,
                                     int x, int y, int w, int h, int skipleft,
                                     int startline, unsigned int **glyphs,
                                     int glyphWidth, int fg, int bg, int rop,
                                     unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererLSBFirst(ScrnInfoPtr pScrn,
                           int x, int y, int w, int h, int skipleft,
                           int startline, unsigned int **glyphs, int glyphWidth,
                           int fg, int bg, int rop, unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRenderer3LSBFirst(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h, int skipleft,
                            int startline, unsigned int **glyphs,
                            int glyphWidth, int fg, int bg, int rop,
                            unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererScanlineMSBFirst(ScrnInfoPtr pScrn,
                                   int x, int y, int w, int h, int skipleft,
                                   int startline, unsigned int **glyphs,
                                   int glyphWidth, int fg, int bg, int rop,
                                   unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererScanline3MSBFirst(ScrnInfoPtr pScrn,
                                    int x, int y, int w, int h, int skipleft,
                                    int startline, unsigned int **glyphs,
                                    int glyphWidth, int fg, int bg, int rop,
                                    unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererScanlineLSBFirst(ScrnInfoPtr pScrn,
                                   int x, int y, int w, int h, int skipleft,
                                   int startline, unsigned int **glyphs,
                                   int glyphWidth, int fg, int bg, int rop,
                                   unsigned planemask);

extern _X_EXPORT void

XAATEGlyphRendererScanline3LSBFirst(ScrnInfoPtr pScrn,
                                    int x, int y, int w, int h, int skipleft,
                                    int startline, unsigned int **glyphs,
                                    int glyphWidth, int fg, int bg, int rop,
                                    unsigned planemask);

extern _X_EXPORT CARD32 *(*XAAGlyphScanlineFuncMSBFirstFixedBase[32]) (CARD32
                                                                       *base,
                                                                       unsigned
                                                                       int
                                                                       **glyphp,
                                                                       int line,
                                                                       int
                                                                       nglyph,
                                                                       int
                                                                       width);

extern _X_EXPORT CARD32 *(*XAAGlyphScanlineFuncMSBFirst[32]) (CARD32 *base,
                                                              unsigned int
                                                              **glyphp,
                                                              int line,
                                                              int nglyph,
                                                              int width);

extern _X_EXPORT CARD32 *(*XAAGlyphScanlineFuncLSBFirstFixedBase[32]) (CARD32
                                                                       *base,
                                                                       unsigned
                                                                       int
                                                                       **glyphp,
                                                                       int line,
                                                                       int
                                                                       nglyph,
                                                                       int
                                                                       width);

extern _X_EXPORT CARD32 *(*XAAGlyphScanlineFuncLSBFirst[32]) (CARD32 *base,
                                                              unsigned int
                                                              **glyphp,
                                                              int line,
                                                              int nglyph,
                                                              int width);

extern _X_EXPORT GlyphScanlineFuncPtr
    *XAAGetGlyphScanlineFuncMSBFirstFixedBase(void);
extern _X_EXPORT GlyphScanlineFuncPtr *XAAGetGlyphScanlineFuncMSBFirst(void);
extern _X_EXPORT GlyphScanlineFuncPtr
    *XAAGetGlyphScanlineFuncLSBFirstFixedBase(void);
extern _X_EXPORT GlyphScanlineFuncPtr *XAAGetGlyphScanlineFuncLSBFirst(void);

extern _X_EXPORT void

XAAFillColorExpandRectsLSBFirst(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop,
                                unsigned int planemask,
                                int nBox,
                                BoxPtr pBox,
                                int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRects3LSBFirst(ScrnInfoPtr pScrn,
                                 int fg, int bg, int rop,
                                 unsigned int planemask,
                                 int nBox,
                                 BoxPtr pBox,
                                 int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRectsLSBFirstFixedBase(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int nBox,
                                         BoxPtr pBox,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRects3LSBFirstFixedBase(ScrnInfoPtr pScrn,
                                          int fg, int bg, int rop,
                                          unsigned int planemask,
                                          int nBox,
                                          BoxPtr pBox,
                                          int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRectsMSBFirst(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop,
                                unsigned int planemask,
                                int nBox,
                                BoxPtr pBox,
                                int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRects3MSBFirst(ScrnInfoPtr pScrn,
                                 int fg, int bg, int rop,
                                 unsigned int planemask,
                                 int nBox,
                                 BoxPtr pBox,
                                 int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRectsMSBFirstFixedBase(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int nBox,
                                         BoxPtr pBox,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandRects3MSBFirstFixedBase(ScrnInfoPtr pScrn,
                                          int fg, int bg, int rop,
                                          unsigned int planemask,
                                          int nBox,
                                          BoxPtr pBox,
                                          int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandRectsLSBFirst(ScrnInfoPtr pScrn,
                                        int fg, int bg, int rop,
                                        unsigned int planemask,
                                        int nBox,
                                        BoxPtr pBox,
                                        int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandRects3LSBFirst(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int nBox,
                                         BoxPtr pBox,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandRectsMSBFirst(ScrnInfoPtr pScrn,
                                        int fg, int bg, int rop,
                                        unsigned int planemask,
                                        int nBox,
                                        BoxPtr pBox,
                                        int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandRects3MSBFirst(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int nBox,
                                         BoxPtr pBox,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpansLSBFirst(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop,
                                unsigned int planemask,
                                int n,
                                DDXPointPtr ppt,
                                int *pwidth,
                                int fSorted,
                                int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpans3LSBFirst(ScrnInfoPtr pScrn,
                                 int fg, int bg, int rop,
                                 unsigned int planemask,
                                 int n,
                                 DDXPointPtr ppt,
                                 int *pwidth,
                                 int fSorted,
                                 int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpansLSBFirstFixedBase(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int n,
                                         DDXPointPtr ppt,
                                         int *pwidth,
                                         int fSorted,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpans3LSBFirstFixedBase(ScrnInfoPtr pScrn,
                                          int fg, int bg, int rop,
                                          unsigned int planemask,
                                          int n,
                                          DDXPointPtr ppt,
                                          int *pwidth,
                                          int fSorted,
                                          int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpansMSBFirst(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop,
                                unsigned int planemask,
                                int n,
                                DDXPointPtr ppt,
                                int *pwidth,
                                int fSorted,
                                int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpans3MSBFirst(ScrnInfoPtr pScrn,
                                 int fg, int bg, int rop,
                                 unsigned int planemask,
                                 int n,
                                 DDXPointPtr ppt,
                                 int *pwidth,
                                 int fSorted,
                                 int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpansMSBFirstFixedBase(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int n,
                                         DDXPointPtr ppt,
                                         int *pwidth,
                                         int fSorted,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillColorExpandSpans3MSBFirstFixedBase(ScrnInfoPtr pScrn,
                                          int fg, int bg, int rop,
                                          unsigned int planemask,
                                          int n,
                                          DDXPointPtr ppt,
                                          int *pwidth,
                                          int fSorted,
                                          int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandSpansLSBFirst(ScrnInfoPtr pScrn,
                                        int fg, int bg, int rop,
                                        unsigned int planemask,
                                        int n,
                                        DDXPointPtr ppt,
                                        int *pwidth,
                                        int fSorted,
                                        int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandSpans3LSBFirst(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int n,
                                         DDXPointPtr ppt,
                                         int *pwidth,
                                         int fSorted,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAPutImage(DrawablePtr pDraw,
            GCPtr pGC,
            int depth,
            int x, int y, int w, int h, int leftPad, int format, char *pImage);

extern _X_EXPORT void

XAAFillScanlineColorExpandSpansMSBFirst(ScrnInfoPtr pScrn,
                                        int fg, int bg, int rop,
                                        unsigned int planemask,
                                        int n,
                                        DDXPointPtr ppt,
                                        int *pwidth,
                                        int fSorted,
                                        int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillScanlineColorExpandSpans3MSBFirst(ScrnInfoPtr pScrn,
                                         int fg, int bg, int rop,
                                         unsigned int planemask,
                                         int n,
                                         DDXPointPtr ppt,
                                         int *pwidth,
                                         int fSorted,
                                         int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT CARD32 *(*XAAStippleScanlineFuncMSBFirstFixedBase[6]) (CARD32
                                                                        *base,
                                                                        CARD32
                                                                        *src,
                                                                        int
                                                                        offset,
                                                                        int
                                                                        width,
                                                                        int
                                                                        dwords);

extern _X_EXPORT CARD32 *(*XAAStippleScanlineFuncMSBFirst[6]) (CARD32 *base,
                                                               CARD32 *src,
                                                               int offset,
                                                               int width,
                                                               int dwords);

extern _X_EXPORT CARD32 *(*XAAStippleScanlineFuncLSBFirstFixedBase[6]) (CARD32
                                                                        *base,
                                                                        CARD32
                                                                        *src,
                                                                        int
                                                                        offset,
                                                                        int
                                                                        width,
                                                                        int
                                                                        dwords);

extern _X_EXPORT CARD32 *(*XAAStippleScanlineFuncLSBFirst[6]) (CARD32 *base,
                                                               CARD32 *src,
                                                               int offset,
                                                               int width,
                                                               int dwords);

extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFuncMSBFirstFixedBase(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFuncMSBFirst(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFuncLSBFirstFixedBase(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFuncLSBFirst(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFunc3MSBFirstFixedBase(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFunc3MSBFirst(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFunc3LSBFirstFixedBase(void);
extern _X_EXPORT StippleScanlineProcPtr
    *XAAGetStippleScanlineFunc3LSBFirst(void);

extern _X_EXPORT int

XAAPolyText8TEColorExpansion(DrawablePtr pDraw,
                             GCPtr pGC, int x, int y, int count, char *chars);

extern _X_EXPORT int

XAAPolyText16TEColorExpansion(DrawablePtr pDraw,
                              GCPtr pGC,
                              int x, int y, int count, unsigned short *chars);

extern _X_EXPORT void

XAAImageText8TEColorExpansion(DrawablePtr pDraw,
                              GCPtr pGC, int x, int y, int count, char *chars);

extern _X_EXPORT void

XAAImageText16TEColorExpansion(DrawablePtr pDraw,
                               GCPtr pGC,
                               int x, int y, int count, unsigned short *chars);

extern _X_EXPORT void

XAAImageGlyphBltTEColorExpansion(DrawablePtr pDrawable,
                                 GCPtr pGC,
                                 int xInit, int yInit,
                                 unsigned int nglyph,
                                 CharInfoPtr * ppci, pointer pglyphBase);

extern _X_EXPORT void

XAAPolyGlyphBltTEColorExpansion(DrawablePtr pDrawable,
                                GCPtr pGC,
                                int xInit, int yInit,
                                unsigned int nglyph,
                                CharInfoPtr * ppci, pointer pglyphBase);

extern _X_EXPORT int

XAAPolyText8NonTEColorExpansion(DrawablePtr pDraw,
                                GCPtr pGC,
                                int x, int y, int count, char *chars);

extern _X_EXPORT int

XAAPolyText16NonTEColorExpansion(DrawablePtr pDraw,
                                 GCPtr pGC,
                                 int x, int y,
                                 int count, unsigned short *chars);

extern _X_EXPORT void

XAAImageText8NonTEColorExpansion(DrawablePtr pDraw,
                                 GCPtr pGC,
                                 int x, int y, int count, char *chars);

extern _X_EXPORT void

XAAImageText16NonTEColorExpansion(DrawablePtr pDraw,
                                  GCPtr pGC,
                                  int x, int y,
                                  int count, unsigned short *chars);

extern _X_EXPORT void

XAAImageGlyphBltNonTEColorExpansion(DrawablePtr pDrawable,
                                    GCPtr pGC,
                                    int xInit, int yInit,
                                    unsigned int nglyph,
                                    CharInfoPtr * ppci, pointer pglyphBase);

extern _X_EXPORT void

XAAPolyGlyphBltNonTEColorExpansion(DrawablePtr pDrawable,
                                   GCPtr pGC,
                                   int xInit, int yInit,
                                   unsigned int nglyph,
                                   CharInfoPtr * ppci, pointer pglyphBase);

extern _X_EXPORT void XAANonTEGlyphRenderer(ScrnInfoPtr pScrn,
                                            int x, int y, int n,
                                            NonTEGlyphPtr glyphs,
                                            BoxPtr pbox,
                                            int fg, int rop,
                                            unsigned int planemask);

extern _X_EXPORT void

XAAFillSolidSpans(ScrnInfoPtr pScrn,
                  int fg, int rop,
                  unsigned int planemask,
                  int n, DDXPointPtr ppt, int *pwidth, int fSorted);

extern _X_EXPORT void

XAAFillMono8x8PatternSpans(ScrnInfoPtr pScrn,
                           int fg, int bg, int rop,
                           unsigned int planemask,
                           int n,
                           DDXPointPtr ppt,
                           int *pwidth, int fSorted,
                           int patx, int paty, int xorg, int yorg);

extern _X_EXPORT void

XAAFillMono8x8PatternSpansScreenOrigin(ScrnInfoPtr pScrn,
                                       int fg, int bg, int rop,
                                       unsigned int planemask,
                                       int n,
                                       DDXPointPtr ppt,
                                       int *pwidth, int fSorted,
                                       int patx, int paty, int xorg, int yorg);

extern _X_EXPORT void

XAAFillColor8x8PatternSpansScreenOrigin(ScrnInfoPtr pScrn,
                                        int rop,
                                        unsigned int planemask,
                                        int n,
                                        DDXPointPtr ppt,
                                        int *pwidth, int fSorted,
                                        XAACacheInfoPtr,
                                        int xorigin, int yorigin);

extern _X_EXPORT void

XAAFillColor8x8PatternSpans(ScrnInfoPtr pScrn,
                            int rop,
                            unsigned int planemask,
                            int n,
                            DDXPointPtr ppt,
                            int *pwidth, int fSorted,
                            XAACacheInfoPtr, int xorigin, int yorigin);

extern _X_EXPORT void

XAAFillCacheBltSpans(ScrnInfoPtr pScrn,
                     int rop,
                     unsigned int planemask,
                     int n,
                     DDXPointPtr points,
                     int *widths,
                     int fSorted, XAACacheInfoPtr pCache, int xorg, int yorg);

extern _X_EXPORT void

XAAFillCacheExpandSpans(ScrnInfoPtr pScrn,
                        int fg, int bg, int rop,
                        unsigned int planemask,
                        int n,
                        DDXPointPtr ppt,
                        int *pwidth,
                        int fSorted, int xorg, int yorg, PixmapPtr pPix);

extern _X_EXPORT void

XAAFillSpans(DrawablePtr pDrawable,
             GC * pGC,
             int nInit, DDXPointPtr pptInit, int *pwidth, int fSorted);

extern _X_EXPORT void
 XAAInitPixmapCache(ScreenPtr pScreen, RegionPtr areas, pointer data);

extern _X_EXPORT void

XAAWriteBitmapToCache(ScrnInfoPtr pScrn,
                      int x, int y, int w, int h,
                      unsigned char *src, int srcwidth, int fg, int bg);

extern _X_EXPORT void

XAAWriteBitmapToCacheLinear(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h,
                            unsigned char *src, int srcwidth, int fg, int bg);

extern _X_EXPORT void

XAAWritePixmapToCache(ScrnInfoPtr pScrn,
                      int x, int y, int w, int h,
                      unsigned char *src, int srcwidth, int bpp, int depth);

extern _X_EXPORT void

XAAWritePixmapToCacheLinear(ScrnInfoPtr pScrn,
                            int x, int y, int w, int h,
                            unsigned char *src,
                            int srcwidth, int bpp, int depth);

extern _X_EXPORT void
 XAASolidHorVertLineAsRects(ScrnInfoPtr pScrn, int x, int y, int len, int dir);

extern _X_EXPORT void

XAASolidHorVertLineAsTwoPoint(ScrnInfoPtr pScrn,
                              int x, int y, int len, int dir);

extern _X_EXPORT void

XAASolidHorVertLineAsBresenham(ScrnInfoPtr pScrn,
                               int x, int y, int len, int dir);

extern _X_EXPORT void

XAAPolyRectangleThinSolid(DrawablePtr pDrawable,
                          GCPtr pGC, int nRectsInit, xRectangle *pRectsInit);

extern _X_EXPORT void

XAAPolylinesWideSolid(DrawablePtr pDrawable,
                      GCPtr pGC, int mode, int npt, DDXPointPtr pPts);

extern _X_EXPORT void

XAAFillPolygonSolid(DrawablePtr pDrawable,
                    GCPtr pGC,
                    int shape, int mode, int count, DDXPointPtr ptsIn);

extern _X_EXPORT void

XAAFillPolygonStippled(DrawablePtr pDrawable,
                       GCPtr pGC,
                       int shape, int mode, int count, DDXPointPtr ptsIn);

extern _X_EXPORT void

XAAFillPolygonTiled(DrawablePtr pDrawable,
                    GCPtr pGC,
                    int shape, int mode, int count, DDXPointPtr ptsIn);

extern _X_EXPORT int

XAAIsEasyPolygon(DDXPointPtr ptsIn,
                 int count,
                 BoxPtr extents,
                 int origin,
                 DDXPointPtr * topPoint, int *topY, int *bottomY, int shape);

extern _X_EXPORT void

XAAFillPolygonHelper(ScrnInfoPtr pScrn,
                     DDXPointPtr ptsIn,
                     int count,
                     DDXPointPtr topPoint,
                     int y,
                     int maxy,
                     int origin,
                     RectFuncPtr RectFunc,
                     TrapFuncPtr TrapFunc,
                     int xorg, int yorg, XAACacheInfoPtr pCache);

extern _X_EXPORT void
 XAAPolySegment(DrawablePtr pDrawable, GCPtr pGC, int nseg, xSegment * pSeg);

extern _X_EXPORT void

XAAPolyLines(DrawablePtr pDrawable,
             GCPtr pGC, int mode, int npt, DDXPointPtr pptInit);

extern _X_EXPORT void

XAAPolySegmentDashed(DrawablePtr pDrawable,
                     GCPtr pGC, int nseg, xSegment * pSeg);

extern _X_EXPORT void

XAAPolyLinesDashed(DrawablePtr pDrawable,
                   GCPtr pGC, int mode, int npt, DDXPointPtr pptInit);

extern _X_EXPORT void
 XAAWriteMono8x8PatternToCache(ScrnInfoPtr pScrn, XAACacheInfoPtr pCache);

extern _X_EXPORT void

XAAWriteColor8x8PatternToCache(ScrnInfoPtr pScrn,
                               PixmapPtr pPix, XAACacheInfoPtr pCache);

extern _X_EXPORT void

XAARotateMonoPattern(int *pat0, int *pat1,
                     int xoffset, int yoffset, Bool msbfirst);

extern _X_EXPORT void XAAComputeDash(GCPtr pGC);

extern _X_EXPORT void XAAMoveDWORDS_FixedBase(register CARD32 *dest,
                                              register CARD32 *src,
                                              register int dwords);

extern _X_EXPORT void XAAMoveDWORDS_FixedSrc(register CARD32 *dest,
                                             register CARD32 *src,
                                             register int dwords);

extern _X_EXPORT void XAAMoveDWORDS(register CARD32 *dest,
                                    register CARD32 *src, register int dwords);

extern _X_EXPORT int

XAAGetRectClipBoxes(GCPtr pGC,
                    BoxPtr pboxClippedBase,
                    int nrectFill, xRectangle *prectInit);

extern _X_EXPORT void
 XAASetupOverlay8_32Planar(ScreenPtr);

extern _X_EXPORT void
 XAAPolyFillArcSolid(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs);

extern _X_EXPORT XAACacheInfoPtr XAACacheTile(ScrnInfoPtr Scrn, PixmapPtr pPix);

extern _X_EXPORT XAACacheInfoPtr
XAACacheMonoStipple(ScrnInfoPtr Scrn, PixmapPtr pPix);

extern _X_EXPORT XAACacheInfoPtr
XAACachePlanarMonoStipple(ScrnInfoPtr Scrn, PixmapPtr pPix);

typedef XAACacheInfoPtr(*XAACachePlanarMonoStippleProc) (ScrnInfoPtr,
                                                         PixmapPtr);
extern _X_EXPORT XAACachePlanarMonoStippleProc
XAAGetCachePlanarMonoStipple(void);

extern _X_EXPORT XAACacheInfoPtr
XAACacheStipple(ScrnInfoPtr Scrn, PixmapPtr pPix, int fg, int bg);

extern _X_EXPORT XAACacheInfoPtr
XAACacheMono8x8Pattern(ScrnInfoPtr Scrn, int pat0, int pat1);

extern _X_EXPORT XAACacheInfoPtr
XAACacheColor8x8Pattern(ScrnInfoPtr Scrn, PixmapPtr pPix, int fg, int bg);

extern _X_EXPORT void
 XAATileCache(ScrnInfoPtr pScrn, XAACacheInfoPtr pCache, int w, int h);

extern _X_EXPORT void XAAClosePixmapCache(ScreenPtr pScreen);
void XAAInvalidatePixmapCache(ScreenPtr pScreen);

extern _X_EXPORT Bool XAACheckStippleReducibility(PixmapPtr pPixmap);
extern _X_EXPORT Bool XAACheckTileReducibility(PixmapPtr pPixmap,
                                               Bool checkMono);

extern _X_EXPORT int XAAStippledFillChooser(GCPtr pGC);
extern _X_EXPORT int XAAOpaqueStippledFillChooser(GCPtr pGC);
extern _X_EXPORT int XAATiledFillChooser(GCPtr pGC);

extern _X_EXPORT void XAAMoveInOffscreenPixmaps(ScreenPtr pScreen);
extern _X_EXPORT void XAAMoveOutOffscreenPixmaps(ScreenPtr pScreen);
extern _X_EXPORT void XAARemoveAreaCallback(FBAreaPtr area);
extern _X_EXPORT void XAAMoveOutOffscreenPixmap(PixmapPtr pPix);
extern _X_EXPORT Bool XAAInitStateWrap(ScreenPtr pScreen,
                                       XAAInfoRecPtr infoRec);

extern _X_EXPORT void

XAAComposite(CARD8 op,
             PicturePtr pSrc,
             PicturePtr pMask,
             PicturePtr pDst,
             INT16 xSrc,
             INT16 ySrc,
             INT16 xMask,
             INT16 yMask, INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);

extern _X_EXPORT Bool

XAADoComposite(CARD8 op,
               PicturePtr pSrc,
               PicturePtr pMask,
               PicturePtr pDst,
               INT16 xSrc,
               INT16 ySrc,
               INT16 xMask,
               INT16 yMask,
               INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);

extern _X_EXPORT void

XAAGlyphs(CARD8 op,
          PicturePtr pSrc,
          PicturePtr pDst,
          PictFormatPtr maskFormat,
          INT16 xSrc,
          INT16 ySrc, int nlist, GlyphListPtr list, GlyphPtr * glyphs);

extern _X_EXPORT Bool

XAADoGlyphs(CARD8 op,
            PicturePtr pSrc,
            PicturePtr pDst,
            PictFormatPtr maskFormat,
            INT16 xSrc,
            INT16 ySrc, int nlist, GlyphListPtr list, GlyphPtr * glyphs);

/* helpers */
extern _X_EXPORT void
 XAA_888_plus_PICT_a8_to_8888(CARD32 color, CARD8 *alphaPtr,    /* in bytes */
                              int alphaPitch, CARD32 *dstPtr, int dstPitch,     /* in dwords */
                              int width, int height);

extern _X_EXPORT Bool

XAAGetRGBAFromPixel(CARD32 pixel,
                    CARD16 *red,
                    CARD16 *green, CARD16 *blue, CARD16 *alpha, CARD32 format);

extern _X_EXPORT Bool

XAAGetPixelFromRGBA(CARD32 *pixel,
                    CARD16 red,
                    CARD16 green, CARD16 blue, CARD16 alpha, CARD32 format);

/* XXX should be static */
extern _X_EXPORT GCOps XAAFallbackOps;
extern _X_EXPORT GCOps *XAAGetFallbackOps(void);
extern _X_EXPORT GCFuncs XAAGCFuncs;
extern _X_EXPORT DevPrivateKey XAAGetScreenKey(void);
extern _X_EXPORT DevPrivateKey XAAGetGCKey(void);
extern _X_EXPORT DevPrivateKey XAAGetPixmapKey(void);

extern _X_EXPORT unsigned int XAAShiftMasks[32];

extern _X_EXPORT unsigned int byte_expand3[256], byte_reversed_expand3[256];

extern _X_EXPORT CARD32 XAAReverseBitOrder(CARD32 data);

#define GET_XAASCREENPTR_FROM_SCREEN(pScreen)\
    dixLookupPrivate(&(pScreen)->devPrivates, XAAGetScreenKey())

#define GET_XAASCREENPTR_FROM_GC(pGC)\
    dixLookupPrivate(&(pGC)->pScreen->devPrivates, XAAGetScreenKey())

#define GET_XAASCREENPTR_FROM_DRAWABLE(pDraw)\
    dixLookupPrivate(&(pDraw)->pScreen->devPrivates, XAAGetScreenKey())

#define GET_XAAINFORECPTR_FROM_SCREEN(pScreen)\
((XAAScreenPtr)dixLookupPrivate(&(pScreen)->devPrivates, XAAGetScreenKey()))->AccelInfoRec

#define GET_XAAINFORECPTR_FROM_GC(pGC)\
((XAAScreenPtr)dixLookupPrivate(&(pGC)->pScreen->devPrivates, XAAGetScreenKey()))->AccelInfoRec

#define GET_XAAINFORECPTR_FROM_DRAWABLE(pDraw)\
((XAAScreenPtr)dixLookupPrivate(&(pDraw)->pScreen->devPrivates, XAAGetScreenKey()))->AccelInfoRec

#define GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn)\
((XAAScreenPtr)dixLookupPrivate(&(pScrn)->pScreen->devPrivates, XAAGetScreenKey()))->AccelInfoRec

#define XAA_GET_PIXMAP_PRIVATE(pix)\
    (XAAPixmapPtr)dixLookupPrivate(&(pix)->devPrivates, XAAGetPixmapKey())

#define CHECK_RGB_EQUAL(c) (!((((c) >> 8) ^ (c)) & 0xffff))

#define CHECK_FG(pGC, flags) \
	(!(flags & RGB_EQUAL) || CHECK_RGB_EQUAL(pGC->fgPixel))

#define CHECK_BG(pGC, flags) \
	(!(flags & RGB_EQUAL) || CHECK_RGB_EQUAL(pGC->bgPixel))

#define CHECK_ROP(pGC, flags) \
	(!(flags & GXCOPY_ONLY) || (pGC->alu == GXcopy))

#define CHECK_ROPSRC(pGC, flags) \
	(!(flags & ROP_NEEDS_SOURCE) || ((pGC->alu != GXclear) && \
	(pGC->alu != GXnoop) && (pGC->alu != GXinvert) && \
	(pGC->alu != GXset)))

#define CHECK_PLANEMASK(pGC, flags) \
	(!(flags & NO_PLANEMASK) || \
	((pGC->planemask & infoRec->FullPlanemasks[pGC->depth - 1]) == \
          infoRec->FullPlanemasks[pGC->depth - 1]))

#define CHECK_COLORS(pGC, flags) \
	(!(flags & RGB_EQUAL) || \
	(CHECK_RGB_EQUAL(pGC->fgPixel) && CHECK_RGB_EQUAL(pGC->bgPixel)))

#define CHECK_NO_GXCOPY(pGC, flags) \
	((pGC->alu != GXcopy) || !(flags & NO_GXCOPY) || \
	((pGC->planemask & infoRec->FullPlanemask) != infoRec->FullPlanemask))

#define IS_OFFSCREEN_PIXMAP(pPix)\
        ((XAA_GET_PIXMAP_PRIVATE((PixmapPtr)(pPix)))->offscreenArea)

#define PIXMAP_IS_SHARED(pPix)\
        ((XAA_GET_PIXMAP_PRIVATE((PixmapPtr)(pPix)))->flags & SHARED_PIXMAP)

#define OFFSCREEN_PIXMAP_LOCKED(pPix)\
        ((XAA_GET_PIXMAP_PRIVATE((PixmapPtr)(pPix)))->flags & LOCKED_PIXMAP)

#define XAA_DEPTH_BUG(pGC) \
        ((pGC->depth == 32) && (pGC->bgPixel == 0xffffffff))

#define DELIST_OFFSCREEN_PIXMAP(pPix) { \
	PixmapLinkPtr _pLink, _prev; \
	_pLink = infoRec->OffscreenPixmaps; \
	_prev = NULL; \
	while(_pLink) { \
	    if(_pLink->pPix == pPix) { \
		if(_prev) _prev->next = _pLink->next; \
		else infoRec->OffscreenPixmaps = _pLink->next; \
		free(_pLink); \
		break; \
	    } \
	    _prev = _pLink; \
	    _pLink = _pLink->next; \
        }}

#define SWAP_BITS_IN_BYTES(v) \
 (((0x01010101 & (v)) << 7) | ((0x02020202 & (v)) << 5) | \
  ((0x04040404 & (v)) << 3) | ((0x08080808 & (v)) << 1) | \
  ((0x10101010 & (v)) >> 1) | ((0x20202020 & (v)) >> 3) | \
  ((0x40404040 & (v)) >> 5) | ((0x80808080 & (v)) >> 7))

/*
 * Moved XAAPixmapCachePrivate here from xaaPCache.c, since driver
 * replacements for CacheMonoStipple need access to it
 */

typedef struct {
    int Num512x512;
    int Current512;
    XAACacheInfoPtr Info512;
    int Num256x256;
    int Current256;
    XAACacheInfoPtr Info256;
    int Num128x128;
    int Current128;
    XAACacheInfoPtr Info128;
    int NumMono;
    int CurrentMono;
    XAACacheInfoPtr InfoMono;
    int NumColor;
    int CurrentColor;
    XAACacheInfoPtr InfoColor;
    int NumPartial;
    int CurrentPartial;
    XAACacheInfoPtr InfoPartial;
    DDXPointRec MonoOffsets[64];
    DDXPointRec ColorOffsets[64];
} XAAPixmapCachePrivate, *XAAPixmapCachePrivatePtr;

#endif                          /* _XAALOCAL_H */
