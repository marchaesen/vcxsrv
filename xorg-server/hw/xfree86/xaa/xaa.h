
#ifndef _XAA_H
#define _XAA_H

#define XAA_VERSION_MAJOR   1
#define XAA_VERSION_MINOR   2
#define XAA_VERSION_RELEASE 1

/*

   ******** OPERATION SPECIFIC FLAGS *********

   **** solid/dashed line flags ****
 
---------               --------
23           LINE_PATTERN_LSBFIRST_MSBJUSTIFIED
22           LINE_PATTERN_LSBFIRST_LSBJUSTIFIED
21           LINE_PATTERN_MSBFIRST_MSBJUSTIFIED
20           LINE_PATTERN_MSBFIRST_LSBJUSTIFIED
19           LINE_PATTERN_POWER_OF_2_ONLY
18           LINE_LIMIT_COORDS
17                         .
16                         .
---------               -------

   **** screen to screen copy flags ****

---------               --------
23           ONLY_LEFT_TO_RIGHT_BITBLT
22           ONLY_TWO_BITBLT_DIRECTIONS
21                         .
20                         .
19                         .
18                         .
17                         .
16                         .
---------               -------

   ****  clipping flags ****

---------               --------
23                         .
22           HARDWARE_CLIP_SCREEN_TO_SCREEN_COLOR_EXPAND
21           HARDWARE_CLIP_SCREEN_TO_SCREEN_COPY
20           HARDWARE_CLIP_MONO_8x8_FILL
19           HARDWARE_CLIP_COLOR_8x8_FILL    
18           HARDWARE_CLIP_SOLID_FILL
17           HARDWARE_CLIP_DASHED_LINE
16           HARDWARE_CLIP_SOLID_LINE
---------               -------

   ****  hardware pattern flags ****

---------               --------
23                         .
22                         .
21           HARDWARE_PATTERN_SCREEN_ORIGIN
20                         .
19                         .
18                         .
17           HARDWARE_PATTERN_PROGRAMMED_ORIGIN
16           HARDWARE_PATTERN_PROGRAMMED_BITS
---------               -------

   ****  write pixmap flags ****

---------               --------
23                         .
22                         .
21                         .
20                         .
19                         .
18                         .
17                         .
16           CONVERT_32BPP_TO_24BPP
---------               -------

   ******** GENERIC FLAGS *********

---------               -------
15           SYNC_AFTER_COLOR_EXPAND
14           CPU_TRANSFER_PAD_QWORD
13                         .
12           LEFT_EDGE_CLIPPING_NEGATIVE_X
11	     LEFT_EDGE_CLIPPING
10	     CPU_TRANSFER_BASE_FIXED
 9           BIT_ORDER_IN_BYTE_MSBFIRST           
 8           TRANSPARENCY_GXCOPY_ONLY
---------               -------
 7           NO_TRANSPARENCY
 6           TRANSPARENCY_ONLY
 5           ROP_NEEDS_SOURCE
 4           TRIPLE_BITS_24BPP
 3           RGB_EQUAL
 2           NO_PLANEMASK
 1           NO_GXCOPY
 0           GXCOPY_ONLY
---------               -------


*/

#include "gcstruct.h"
#include "pixmapstr.h"
#include "xf86str.h"
#include "regionstr.h"
#include "xf86fbman.h"

#include "picturestr.h"

/* Flags */
#define PIXMAP_CACHE			0x00000001
#define MICROSOFT_ZERO_LINE_BIAS	0x00000002
#define OFFSCREEN_PIXMAPS		0x00000004
#define LINEAR_FRAMEBUFFER		0x00000008

/* GC fg, bg, and planemask restrictions */
#define GXCOPY_ONLY			0x00000001
#define NO_GXCOPY			0x00000002
#define NO_PLANEMASK			0x00000004
#define RGB_EQUAL			0x00000008
#define TRIPLE_BITS_24BPP		0x00000010
#define ROP_NEEDS_SOURCE		0x00000020

/* transparency restrictions */
#define TRANSPARENCY_ONLY		0x00000040
#define NO_TRANSPARENCY			0x00000080
#define TRANSPARENCY_GXCOPY_ONLY     	0x00000100

/* bit order restrictions */
#define BIT_ORDER_IN_BYTE_MSBFIRST	0x00000200
#define BIT_ORDER_IN_BYTE_LSBFIRST	0x00000000

/* transfer base restriction */
#define CPU_TRANSFER_BASE_FIXED		0x00000400

/* skipleft restrictions */
#define LEFT_EDGE_CLIPPING		0x00000800
#define LEFT_EDGE_CLIPPING_NEGATIVE_X	0x00001000

/* data padding */
#define CPU_TRANSFER_PAD_DWORD		0x00000000
#define CPU_TRANSFER_PAD_QWORD		0x00004000
#define SCANLINE_PAD_DWORD		0x00000000

#define SYNC_AFTER_COLOR_EXPAND		0x00008000
#define SYNC_AFTER_IMAGE_WRITE		SYNC_AFTER_COLOR_EXPAND

/* hardware pattern */
#define HARDWARE_PATTERN_PROGRAMMED_BITS	0x00010000
#define HARDWARE_PATTERN_PROGRAMMED_ORIGIN	0x00020000
#define HARDWARE_PATTERN_SCREEN_ORIGIN		0x00200000

/* copyarea flags */
#define ONLY_TWO_BITBLT_DIRECTIONS	0x00400000
#define ONLY_LEFT_TO_RIGHT_BITBLT	0x00800000

/* line flags */
#define LINE_PATTERN_LSBFIRST_MSBJUSTIFIED	0x00800000
#define LINE_PATTERN_LSBFIRST_LSBJUSTIFIED	0x00400000
#define LINE_PATTERN_MSBFIRST_MSBJUSTIFIED	0x00200000
#define LINE_PATTERN_MSBFIRST_LSBJUSTIFIED	0x00100000
#define LINE_PATTERN_POWER_OF_2_ONLY		0x00080000
#define LINE_LIMIT_COORDS			0x00040000

/* clipping flags */
#define HARDWARE_CLIP_SCREEN_TO_SCREEN_COLOR_EXPAND	0x00400000
#define HARDWARE_CLIP_SCREEN_TO_SCREEN_COPY		0x00200000
#define HARDWARE_CLIP_MONO_8x8_FILL			0x00100000
#define HARDWARE_CLIP_COLOR_8x8_FILL			0x00080000
#define HARDWARE_CLIP_SOLID_FILL			0x00040000
#define HARDWARE_CLIP_DASHED_LINE			0x00020000
#define HARDWARE_CLIP_SOLID_LINE			0x00010000

#define HARDWARE_CLIP_LINE				0x00000000

/* image write flags */
#define CONVERT_32BPP_TO_24BPP			0x00010000

/* pixmap cache flags */
#define CACHE_MONO_8x8			0x00000001
#define CACHE_COLOR_8x8			0x00000002
#define DO_NOT_BLIT_STIPPLES		0x00000004
#define DO_NOT_TILE_MONO_DATA		0x00000008
#define DO_NOT_TILE_COLOR_DATA		0x00000010

#define DEGREES_0	0
#define DEGREES_90	1
#define DEGREES_180	2
#define DEGREES_270	3

#define OMIT_LAST	1

/* render flags */

#define XAA_RENDER_POWER_OF_2_TILE_ONLY	0x00000008
#define XAA_RENDER_NO_SRC_ALPHA		0x00000004
#define XAA_RENDER_IMPRECISE_ONLY	0x00000002
#define XAA_RENDER_NO_TILE		0x00000001

#define XAA_RENDER_REPEAT		0x00000001

typedef void (*ValidateGCProcPtr) (GCPtr pGC,
                                   unsigned long changes, DrawablePtr pDraw);

typedef struct {
    unsigned char *bits;
    int width;
    int height;
    int yoff;
    int srcwidth;
    int start;
    int end;
} NonTEGlyphInfo, *NonTEGlyphPtr;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int orig_w;
    int orig_h;
    unsigned long serialNumber;
    int pat0;
    int pat1;
    int fg;
    int bg;
    int trans_color;
    DDXPointPtr offsets;
    DevUnion devPrivate;
} XAACacheInfoRec, *XAACacheInfoPtr;

typedef struct _PixmapLink {
    PixmapPtr pPix;
    struct _PixmapLink *next;
    FBAreaPtr area;
} PixmapLink, *PixmapLinkPtr;

typedef struct _XAAInfoRec {
    ScrnInfoPtr pScrn;
    int Flags;

    void (*Sync) (ScrnInfoPtr pScrn);

    /* Restore Accel State is a driver callback that is used
     * when another screen on the same device has been active.
     * This allows multihead on a single device to work.
     * If The entityProp has IS_SHARED_ACCEL defined then this
     * function is required.
     */

    void (*RestoreAccelState) (ScrnInfoPtr pScrn);

   /***************** Low Level *****************/

/* Blits */
    void (*SetupForScreenToScreenCopy) (ScrnInfoPtr pScrn,
                                        int xdir, int ydir,
                                        int rop,
                                        unsigned int planemask,
                                        int trans_color);
    int ScreenToScreenCopyFlags;

    void (*SubsequentScreenToScreenCopy) (ScrnInfoPtr pScrn,
                                          int xsrc, int ysrc,
                                          int xdst, int ydst, int w, int h);

/* Solid fills */
    void (*SetupForSolidFill) (ScrnInfoPtr pScrn,
                               int color, int rop, unsigned int planemask);
    int SolidFillFlags;

    void (*SubsequentSolidFillRect) (ScrnInfoPtr pScrn,
                                     int x, int y, int w, int h);

    void (*SubsequentSolidFillTrap) (ScrnInfoPtr pScrn,
                                     int y, int h,
                                     int left, int dxL, int dyL, int eL,
                                     int right, int dxR, int dyR, int eR);

/* Solid lines */

    void (*SetupForSolidLine) (ScrnInfoPtr pScrn,
                               int color, int rop, unsigned int planemask);
    int SolidLineFlags;

    void (*SubsequentSolidTwoPointLine) (ScrnInfoPtr pScrn,
                                         int xa, int ya, int xb, int yb,
                                         int flags);

    void (*SubsequentSolidBresenhamLine) (ScrnInfoPtr pScrn,
                                          int x, int y, int absmaj, int absmin,
                                          int err, int len, int octant);
    int SolidBresenhamLineErrorTermBits;

    void (*SubsequentSolidHorVertLine) (ScrnInfoPtr pScrn,
                                        int x, int y, int len, int dir);

/* Dashed lines */

    void (*SetupForDashedLine) (ScrnInfoPtr pScrn,
                                int fg, int bg,
                                int rop,
                                unsigned int planemask,
                                int length, unsigned char *pattern);
    int DashedLineFlags;
    int DashPatternMaxLength;

    void (*SubsequentDashedTwoPointLine) (ScrnInfoPtr pScrn,
                                          int xa, int ya, int xb, int yb,
                                          int flags, int phase);

    void (*SubsequentDashedBresenhamLine) (ScrnInfoPtr pScrn,
                                           int x, int y, int absmaj, int absmin,
                                           int err, int len, int flags,
                                           int phase);
    int DashedBresenhamLineErrorTermBits;

/* Clipper */

    void (*SetClippingRectangle) (ScrnInfoPtr pScrn,
                                  int left, int top, int right, int bottom);
    int ClippingFlags;

    void (*DisableClipping) (ScrnInfoPtr pScrn);

/* 8x8 mono pattern fills */
    void (*SetupForMono8x8PatternFill) (ScrnInfoPtr pScrn,
                                        int patx, int paty,
                                        int fg, int bg,
                                        int rop, unsigned int planemask);
    int Mono8x8PatternFillFlags;

    void (*SubsequentMono8x8PatternFillRect) (ScrnInfoPtr pScrn,
                                              int patx, int paty,
                                              int x, int y, int w, int h);

    void (*SubsequentMono8x8PatternFillTrap) (ScrnInfoPtr pScrn,
                                              int patx, int paty,
                                              int y, int h,
                                              int left, int dxL, int dyL,
                                              int eL, int right, int dxR,
                                              int dyR, int eR);

/* 8x8 color pattern fills */

    void (*SetupForColor8x8PatternFill) (ScrnInfoPtr pScrn,
                                         int patx, int paty,
                                         int rop,
                                         unsigned int planemask,
                                         int transparency_color);
    int Color8x8PatternFillFlags;

    void (*SubsequentColor8x8PatternFillRect) (ScrnInfoPtr pScrn,
                                               int patx, int paty,
                                               int x, int y, int w, int h);

    void (*SubsequentColor8x8PatternFillTrap) (ScrnInfoPtr pScrn,
                                               int patx, int paty,
                                               int y, int h,
                                               int left, int dxL, int dyL,
                                               int eL, int right, int dxR,
                                               int dyR, int eR);

/* Color expansion */

    void (*SetupForCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                int fg, int bg,
                                                int rop,
                                                unsigned int planemask);
    int CPUToScreenColorExpandFillFlags;

    void (*SubsequentCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                  int x, int y, int w, int h,
                                                  int skipleft);

    unsigned char *ColorExpandBase;
    int ColorExpandRange;

/* Scanline color expansion  */

    void (*SetupForScanlineCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                        int fg, int bg,
                                                        int rop,
                                                        unsigned int planemask);
    int ScanlineCPUToScreenColorExpandFillFlags;

    void (*SubsequentScanlineCPUToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                          int x, int y, int w,
                                                          int h, int skipleft);

    void (*SubsequentColorExpandScanline) (ScrnInfoPtr pScrn, int bufno);

    int NumScanlineColorExpandBuffers;
    unsigned char **ScanlineColorExpandBuffers;

/* Screen to screen color expansion */

    void (*SetupForScreenToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                   int fg, int bg,
                                                   int rop,
                                                   unsigned int planemask);
    int ScreenToScreenColorExpandFillFlags;

    void (*SubsequentScreenToScreenColorExpandFill) (ScrnInfoPtr pScrn,
                                                     int x, int y, int w, int h,
                                                     int srcx, int srcy,
                                                     int skipleft);

/*  Image transfers */

    void (*SetupForImageWrite) (ScrnInfoPtr pScrn,
                                int rop,
                                unsigned int planemask,
                                int transparency_color, int bpp, int depth);
    int ImageWriteFlags;

    void (*SubsequentImageWriteRect) (ScrnInfoPtr pScrn,
                                      int x, int y, int w, int h, int skipleft);
    unsigned char *ImageWriteBase;
    int ImageWriteRange;

/*  Scanline Image transfers */

    void (*SetupForScanlineImageWrite) (ScrnInfoPtr pScrn,
                                        int rop,
                                        unsigned int planemask,
                                        int transparency_color,
                                        int bpp, int depth);
    int ScanlineImageWriteFlags;

    void (*SubsequentScanlineImageWriteRect) (ScrnInfoPtr pScrn,
                                              int x, int y, int w, int h,
                                              int skipleft);

    void (*SubsequentImageWriteScanline) (ScrnInfoPtr pScrn, int bufno);

    int NumScanlineImageWriteBuffers;
    unsigned char **ScanlineImageWriteBuffers;

    /* Image Reads - OBSOLETE AND NOT USED */

    void (*SetupForImageRead) (ScrnInfoPtr pScrn, int bpp, int depth);
    int ImageReadFlags;

    unsigned char *ImageReadBase;
    int ImageReadRange;

    void (*SubsequentImageReadRect) (ScrnInfoPtr pScrn,
                                     int x, int y, int w, int h);

   /***************** Mid Level *****************/
    void (*ScreenToScreenBitBlt) (ScrnInfoPtr pScrn,
                                  int nbox,
                                  DDXPointPtr pptSrc,
                                  BoxPtr pbox,
                                  int xdir, int ydir,
                                  int alu, unsigned int planmask);
    int ScreenToScreenBitBltFlags;

    void (*WriteBitmap) (ScrnInfoPtr pScrn,
                         int x, int y, int w, int h,
                         unsigned char *src,
                         int srcwidth,
                         int skipleft,
                         int fg, int bg, int rop, unsigned int planemask);
    int WriteBitmapFlags;

    void (*FillSolidRects) (ScrnInfoPtr pScrn,
                            int fg, int rop,
                            unsigned int planemask, int nBox, BoxPtr pBox);
    int FillSolidRectsFlags;

    void (*FillMono8x8PatternRects) (ScrnInfoPtr pScrn,
                                     int fg, int bg, int rop,
                                     unsigned int planemask,
                                     int nBox,
                                     BoxPtr pBox,
                                     int pat0, int pat1, int xorg, int yorg);
    int FillMono8x8PatternRectsFlags;

    void (*FillColor8x8PatternRects) (ScrnInfoPtr pScrn,
                                      int rop,
                                      unsigned int planemask,
                                      int nBox,
                                      BoxPtr pBox,
                                      int xorg, int yorg,
                                      XAACacheInfoPtr pCache);
    int FillColor8x8PatternRectsFlags;

    void (*FillCacheBltRects) (ScrnInfoPtr pScrn,
                               int rop,
                               unsigned int planemask,
                               int nBox,
                               BoxPtr pBox,
                               int xorg, int yorg, XAACacheInfoPtr pCache);
    int FillCacheBltRectsFlags;

    void (*FillColorExpandRects) (ScrnInfoPtr pScrn,
                                  int fg, int bg, int rop,
                                  unsigned int planemask,
                                  int nBox,
                                  BoxPtr pBox,
                                  int xorg, int yorg, PixmapPtr pPix);
    int FillColorExpandRectsFlags;

    void (*FillCacheExpandRects) (ScrnInfoPtr pScrn,
                                  int fg, int bg, int rop,
                                  unsigned int planemask,
                                  int nBox,
                                  BoxPtr pBox,
                                  int xorg, int yorg, PixmapPtr pPix);
    int FillCacheExpandRectsFlags;

    void (*FillImageWriteRects) (ScrnInfoPtr pScrn,
                                 int rop,
                                 unsigned int planemask,
                                 int nBox,
                                 BoxPtr pBox,
                                 int xorg, int yorg, PixmapPtr pPix);
    int FillImageWriteRectsFlags;

    void (*FillSolidSpans) (ScrnInfoPtr pScrn,
                            int fg, int rop,
                            unsigned int planemask,
                            int n,
                            DDXPointPtr points, int *widths, int fSorted);
    int FillSolidSpansFlags;

    void (*FillMono8x8PatternSpans) (ScrnInfoPtr pScrn,
                                     int fg, int bg, int rop,
                                     unsigned int planemask,
                                     int n,
                                     DDXPointPtr points,
                                     int *widths,
                                     int fSorted,
                                     int pat0, int pat1, int xorg, int yorg);
    int FillMono8x8PatternSpansFlags;

    void (*FillColor8x8PatternSpans) (ScrnInfoPtr pScrn,
                                      int rop,
                                      unsigned int planemask,
                                      int n,
                                      DDXPointPtr points,
                                      int *widths,
                                      int fSorted,
                                      XAACacheInfoPtr pCache,
                                      int xorg, int yorg);
    int FillColor8x8PatternSpansFlags;

    void (*FillCacheBltSpans) (ScrnInfoPtr pScrn,
                               int rop,
                               unsigned int planemask,
                               int n,
                               DDXPointPtr points,
                               int *widths,
                               int fSorted,
                               XAACacheInfoPtr pCache, int xorg, int yorg);
    int FillCacheBltSpansFlags;

    void (*FillColorExpandSpans) (ScrnInfoPtr pScrn,
                                  int fg, int bg, int rop,
                                  unsigned int planemask,
                                  int n,
                                  DDXPointPtr points,
                                  int *widths,
                                  int fSorted,
                                  int xorg, int yorg, PixmapPtr pPix);
    int FillColorExpandSpansFlags;

    void (*FillCacheExpandSpans) (ScrnInfoPtr pScrn,
                                  int fg, int bg, int rop,
                                  unsigned int planemask,
                                  int n,
                                  DDXPointPtr ppt,
                                  int *pwidth,
                                  int fSorted,
                                  int xorg, int yorg, PixmapPtr pPix);
    int FillCacheExpandSpansFlags;

    void (*TEGlyphRenderer) (ScrnInfoPtr pScrn,
                             int x, int y, int w, int h, int skipleft,
                             int startline, unsigned int **glyphs,
                             int glyphWidth, int fg, int bg, int rop,
                             unsigned planemask);
    int TEGlyphRendererFlags;

    void (*NonTEGlyphRenderer) (ScrnInfoPtr pScrn,
                                int x, int y, int n,
                                NonTEGlyphPtr glyphs,
                                BoxPtr pbox,
                                int fg, int rop, unsigned int planemask);
    int NonTEGlyphRendererFlags;

    void (*WritePixmap) (ScrnInfoPtr pScrn,
                         int x, int y, int w, int h,
                         unsigned char *src,
                         int srcwidth,
                         int rop,
                         unsigned int planemask,
                         int transparency_color, int bpp, int depth);
    int WritePixmapFlags;

    void (*ReadPixmap) (ScrnInfoPtr pScrn,
                        int x, int y, int w, int h,
                        unsigned char *dst, int dstwidth, int bpp, int depth);
    int ReadPixmapFlags;

   /***************** GC Level *****************/
    RegionPtr (*CopyArea) (DrawablePtr pSrcDrawable,
                           DrawablePtr pDstDrawable,
                           GC * pGC,
                           int srcx, int srcy,
                           int width, int height, int dstx, int dsty);
    int CopyAreaFlags;

    RegionPtr (*CopyPlane) (DrawablePtr pSrc,
                            DrawablePtr pDst,
                            GCPtr pGC,
                            int srcx, int srcy,
                            int width, int height,
                            int dstx, int dsty, unsigned long bitPlane);
    int CopyPlaneFlags;

    void (*PushPixelsSolid) (GCPtr pGC,
                             PixmapPtr pBitMap,
                             DrawablePtr pDrawable,
                             int dx, int dy, int xOrg, int yOrg);
    int PushPixelsFlags;

   /** PolyFillRect **/

    void (*PolyFillRectSolid) (DrawablePtr pDraw,
                               GCPtr pGC, int nrectFill, xRectangle *prectInit);
    int PolyFillRectSolidFlags;

    void (*PolyFillRectStippled) (DrawablePtr pDraw,
                                  GCPtr pGC,
                                  int nrectFill, xRectangle *prectInit);
    int PolyFillRectStippledFlags;

    void (*PolyFillRectOpaqueStippled) (DrawablePtr pDraw,
                                        GCPtr pGC,
                                        int nrectFill, xRectangle *prectInit);
    int PolyFillRectOpaqueStippledFlags;

    void (*PolyFillRectTiled) (DrawablePtr pDraw,
                               GCPtr pGC, int nrectFill, xRectangle *prectInit);
    int PolyFillRectTiledFlags;

   /** FillSpans **/

    void (*FillSpansSolid) (DrawablePtr pDraw,
                            GCPtr pGC,
                            int nInit,
                            DDXPointPtr ppt, int *pwidth, int fSorted);
    int FillSpansSolidFlags;

    void (*FillSpansStippled) (DrawablePtr pDraw,
                               GCPtr pGC,
                               int nInit,
                               DDXPointPtr ppt, int *pwidth, int fSorted);
    int FillSpansStippledFlags;

    void (*FillSpansOpaqueStippled) (DrawablePtr pDraw,
                                     GCPtr pGC,
                                     int nInit,
                                     DDXPointPtr ppt, int *pwidth, int fSorted);
    int FillSpansOpaqueStippledFlags;

    void (*FillSpansTiled) (DrawablePtr pDraw,
                            GCPtr pGC,
                            int nInit,
                            DDXPointPtr ppt, int *pwidth, int fSorted);
    int FillSpansTiledFlags;

    int (*PolyText8TE) (DrawablePtr pDraw,
                        GCPtr pGC, int x, int y, int count, char *chars);
    int PolyText8TEFlags;

    int (*PolyText16TE) (DrawablePtr pDraw,
                         GCPtr pGC,
                         int x, int y, int count, unsigned short *chars);
    int PolyText16TEFlags;

    void (*ImageText8TE) (DrawablePtr pDraw,
                          GCPtr pGC, int x, int y, int count, char *chars);
    int ImageText8TEFlags;

    void (*ImageText16TE) (DrawablePtr pDraw,
                           GCPtr pGC,
                           int x, int y, int count, unsigned short *chars);
    int ImageText16TEFlags;

    void (*ImageGlyphBltTE) (DrawablePtr pDrawable,
                             GCPtr pGC,
                             int xInit, int yInit,
                             unsigned int nglyph,
                             CharInfoPtr * ppci, pointer pglyphBase);
    int ImageGlyphBltTEFlags;

    void (*PolyGlyphBltTE) (DrawablePtr pDrawable,
                            GCPtr pGC,
                            int xInit, int yInit,
                            unsigned int nglyph,
                            CharInfoPtr * ppci, pointer pglyphBase);
    int PolyGlyphBltTEFlags;

    int (*PolyText8NonTE) (DrawablePtr pDraw,
                           GCPtr pGC, int x, int y, int count, char *chars);
    int PolyText8NonTEFlags;

    int (*PolyText16NonTE) (DrawablePtr pDraw,
                            GCPtr pGC,
                            int x, int y, int count, unsigned short *chars);
    int PolyText16NonTEFlags;

    void (*ImageText8NonTE) (DrawablePtr pDraw,
                             GCPtr pGC, int x, int y, int count, char *chars);
    int ImageText8NonTEFlags;

    void (*ImageText16NonTE) (DrawablePtr pDraw,
                              GCPtr pGC,
                              int x, int y, int count, unsigned short *chars);
    int ImageText16NonTEFlags;

    void (*ImageGlyphBltNonTE) (DrawablePtr pDrawable,
                                GCPtr pGC,
                                int xInit, int yInit,
                                unsigned int nglyph,
                                CharInfoPtr * ppci, pointer pglyphBase);
    int ImageGlyphBltNonTEFlags;

    void (*PolyGlyphBltNonTE) (DrawablePtr pDrawable,
                               GCPtr pGC,
                               int xInit, int yInit,
                               unsigned int nglyph,
                               CharInfoPtr * ppci, pointer pglyphBase);
    int PolyGlyphBltNonTEFlags;

    void (*PolyRectangleThinSolid) (DrawablePtr pDrawable,
                                    GCPtr pGC,
                                    int nRectsInit, xRectangle *pRectsInit);
    int PolyRectangleThinSolidFlags;

    void (*PolylinesWideSolid) (DrawablePtr pDrawable,
                                GCPtr pGC, int mode, int npt, DDXPointPtr pPts);
    int PolylinesWideSolidFlags;

    void (*PolylinesThinSolid) (DrawablePtr pDrawable,
                                GCPtr pGC, int mode, int npt, DDXPointPtr pPts);
    int PolylinesThinSolidFlags;

    void (*PolySegmentThinSolid) (DrawablePtr pDrawable,
                                  GCPtr pGC, int nseg, xSegment * pSeg);
    int PolySegmentThinSolidFlags;

    void (*PolylinesThinDashed) (DrawablePtr pDrawable,
                                 GCPtr pGC,
                                 int mode, int npt, DDXPointPtr pPts);
    int PolylinesThinDashedFlags;

    void (*PolySegmentThinDashed) (DrawablePtr pDrawable,
                                   GCPtr pGC, int nseg, xSegment * pSeg);
    int PolySegmentThinDashedFlags;

    void (*FillPolygonSolid) (DrawablePtr pDrawable,
                              GCPtr pGC,
                              int shape,
                              int mode, int count, DDXPointPtr ptsIn);
    int FillPolygonSolidFlags;

    void (*FillPolygonStippled) (DrawablePtr pDrawable,
                                 GCPtr pGC,
                                 int shape,
                                 int mode, int count, DDXPointPtr ptsIn);
    int FillPolygonStippledFlags;

    void (*FillPolygonOpaqueStippled) (DrawablePtr pDrawable,
                                       GCPtr pGC,
                                       int shape,
                                       int mode, int count, DDXPointPtr ptsIn);
    int FillPolygonOpaqueStippledFlags;

    void (*FillPolygonTiled) (DrawablePtr pDrawable,
                              GCPtr pGC,
                              int shape,
                              int mode, int count, DDXPointPtr ptsIn);
    int FillPolygonTiledFlags;

    void (*PolyFillArcSolid) (DrawablePtr pDraw,
                              GCPtr pGC, int narcs, xArc * parcs);
    int PolyFillArcSolidFlags;

    void (*PutImage) (DrawablePtr pDraw,
                      GCPtr pGC,
                      int depth,
                      int x,
                      int y,
                      int w, int h, int leftPad, int format, char *pImage);
    int PutImageFlags;

    /* Validation masks */

    unsigned long FillSpansMask;
    ValidateGCProcPtr ValidateFillSpans;
    unsigned long SetSpansMask;
    ValidateGCProcPtr ValidateSetSpans;
    unsigned long PutImageMask;
    ValidateGCProcPtr ValidatePutImage;
    unsigned long CopyAreaMask;
    ValidateGCProcPtr ValidateCopyArea;
    unsigned long CopyPlaneMask;
    ValidateGCProcPtr ValidateCopyPlane;
    unsigned long PolyPointMask;
    ValidateGCProcPtr ValidatePolyPoint;
    unsigned long PolylinesMask;
    ValidateGCProcPtr ValidatePolylines;
    unsigned long PolySegmentMask;
    ValidateGCProcPtr ValidatePolySegment;
    unsigned long PolyRectangleMask;
    ValidateGCProcPtr ValidatePolyRectangle;
    unsigned long PolyArcMask;
    ValidateGCProcPtr ValidatePolyArc;
    unsigned long FillPolygonMask;
    ValidateGCProcPtr ValidateFillPolygon;
    unsigned long PolyFillRectMask;
    ValidateGCProcPtr ValidatePolyFillRect;
    unsigned long PolyFillArcMask;
    ValidateGCProcPtr ValidatePolyFillArc;
    unsigned long PolyText8Mask;
    ValidateGCProcPtr ValidatePolyText8;
    unsigned long PolyText16Mask;
    ValidateGCProcPtr ValidatePolyText16;
    unsigned long ImageText8Mask;
    ValidateGCProcPtr ValidateImageText8;
    unsigned long ImageText16Mask;
    ValidateGCProcPtr ValidateImageText16;
    unsigned long PolyGlyphBltMask;
    ValidateGCProcPtr ValidatePolyGlyphBlt;
    unsigned long ImageGlyphBltMask;
    ValidateGCProcPtr ValidateImageGlyphBlt;
    unsigned long PushPixelsMask;
    ValidateGCProcPtr ValidatePushPixels;

    void (*ComputeDash) (GCPtr pGC);

    /* Pixmap Cache */

    int PixmapCacheFlags;
    Bool UsingPixmapCache;
    Bool CanDoMono8x8;
    Bool CanDoColor8x8;

    void (*InitPixmapCache) (ScreenPtr pScreen, RegionPtr areas, pointer data);
    void (*ClosePixmapCache) (ScreenPtr pScreen);

    int (*StippledFillChooser) (GCPtr pGC);
    int (*OpaqueStippledFillChooser) (GCPtr pGC);
    int (*TiledFillChooser) (GCPtr pGC);

    int CachePixelGranularity;
    int MaxCacheableTileWidth;
    int MaxCacheableTileHeight;
    int MaxCacheableStippleWidth;
    int MaxCacheableStippleHeight;

     XAACacheInfoPtr(*CacheTile) (ScrnInfoPtr Scrn, PixmapPtr pPix);
     XAACacheInfoPtr(*CacheStipple) (ScrnInfoPtr Scrn, PixmapPtr pPix,
                                     int fg, int bg);
     XAACacheInfoPtr(*CacheMonoStipple) (ScrnInfoPtr Scrn, PixmapPtr pPix);
     XAACacheInfoPtr(*CacheMono8x8Pattern) (ScrnInfoPtr Scrn, int pat0,
                                            int pat1);
     XAACacheInfoPtr(*CacheColor8x8Pattern) (ScrnInfoPtr Scrn, PixmapPtr pPix,
                                             int fg, int bg);

    int MonoPatternPitch;
    int CacheWidthMono8x8Pattern;
    int CacheHeightMono8x8Pattern;

    int ColorPatternPitch;
    int CacheWidthColor8x8Pattern;
    int CacheHeightColor8x8Pattern;

    int CacheColorExpandDensity;

    void (*WriteBitmapToCache) (ScrnInfoPtr pScrn,
                                int x, int y, int w, int h,
                                unsigned char *src,
                                int srcwidth, int fg, int bg);
    void (*WritePixmapToCache) (ScrnInfoPtr pScrn,
                                int x, int y, int w, int h,
                                unsigned char *src,
                                int srcwidth, int bpp, int depth);
    void (*WriteMono8x8PatternToCache) (ScrnInfoPtr pScrn,
                                        XAACacheInfoPtr pCache);
    void (*WriteColor8x8PatternToCache) (ScrnInfoPtr pScrn,
                                         PixmapPtr pPix,
                                         XAACacheInfoPtr pCache);

    char *PixmapCachePrivate;

    /* Miscellaneous */

    GC ScratchGC;
    int PreAllocSize;
    unsigned char *PreAllocMem;

    CharInfoPtr CharInfo[255];
    NonTEGlyphInfo GlyphInfo[255];

    unsigned int FullPlanemask; /* deprecated */

    PixmapLinkPtr OffscreenPixmaps;
    int maxOffPixWidth;
    int maxOffPixHeight;

    XAACacheInfoRec ScratchCacheInfoRec;

    BoxPtr ClipBox;

    Bool NeedToSync;

    char *dgaSaves;

    /* These can be supplied to override the defaults */

    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;
    CopyWindowProcPtr CopyWindow;

    unsigned int offscreenDepths;
    Bool offscreenDepthsInitialized;

    CARD32 FullPlanemasks[32];

    Bool (*Composite) (CARD8 op,
                       PicturePtr pSrc,
                       PicturePtr pMask,
                       PicturePtr pDst,
                       INT16 xSrc,
                       INT16 ySrc,
                       INT16 xMask,
                       INT16 yMask,
                       INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);

    Bool (*Glyphs) (CARD8 op,
                    PicturePtr pSrc,
                    PicturePtr pDst,
                    PictFormatPtr maskFormat,
                    INT16 xSrc,
                    INT16 ySrc,
                    int nlist, GlyphListPtr list, GlyphPtr * glyphs);

    /* The old SetupForCPUToScreenAlphaTexture function is no longer used because
     * it doesn't pass in enough information to write a conforming
     * implementation.  See SetupForCPUToScreenAlphaTexture2.
     */
    Bool (*SetupForCPUToScreenAlphaTexture) (ScrnInfoPtr pScrn,
                                             int op,
                                             CARD16 red,
                                             CARD16 green,
                                             CARD16 blue,
                                             CARD16 alpha,
                                             int alphaType,
                                             CARD8 *alphaPtr,
                                             int alphaPitch,
                                             int width, int height, int flags);
    void (*SubsequentCPUToScreenAlphaTexture) (ScrnInfoPtr pScrn,
                                               int dstx,
                                               int dsty,
                                               int srcx,
                                               int srcy, int width, int height);
    int CPUToScreenAlphaTextureFlags;
    CARD32 *CPUToScreenAlphaTextureFormats;

    /* The old SetupForCPUToScreenTexture function is no longer used because
     * it doesn't pass in enough information to write a conforming
     * implementation.  See SetupForCPUToScreenTexture2.
     */
    Bool (*SetupForCPUToScreenTexture) (ScrnInfoPtr pScrn,
                                        int op,
                                        int texType,
                                        CARD8 *texPtr,
                                        int texPitch,
                                        int width, int height, int flags);
    void (*SubsequentCPUToScreenTexture) (ScrnInfoPtr pScrn,
                                          int dstx,
                                          int dsty,
                                          int srcx,
                                          int srcy, int width, int height);
    int CPUToScreenTextureFlags;
    CARD32 *CPUToScreenTextureFormats;

    /* these were added for 4.3.0 */
    BoxRec SolidLineLimits;
    BoxRec DashedLineLimits;

    /* These were added for X.Org 6.8.0 */
    Bool (*SetupForCPUToScreenAlphaTexture2) (ScrnInfoPtr pScrn,
                                              int op,
                                              CARD16 red,
                                              CARD16 green,
                                              CARD16 blue,
                                              CARD16 alpha,
                                              CARD32 maskFormat,
                                              CARD32 dstFormat,
                                              CARD8 *alphaPtr,
                                              int alphaPitch,
                                              int width, int height, int flags);
    CARD32 *CPUToScreenAlphaTextureDstFormats;

    Bool (*SetupForCPUToScreenTexture2) (ScrnInfoPtr pScrn,
                                         int op,
                                         CARD32 srcFormat,
                                         CARD32 dstFormat,
                                         CARD8 *texPtr,
                                         int texPitch,
                                         int width, int height, int flags);
    CARD32 *CPUToScreenTextureDstFormats;
} XAAInfoRec, *XAAInfoRecPtr;

#define SET_SYNC_FLAG(infoRec)	(infoRec)->NeedToSync = TRUE

extern _X_EXPORT Bool
 XAAInit(ScreenPtr pScreen, XAAInfoRecPtr infoRec);

extern _X_EXPORT XAAInfoRecPtr XAACreateInfoRec(void);

extern _X_EXPORT void
 XAADestroyInfoRec(XAAInfoRecPtr infoRec);

typedef void (*DepthChangeFuncPtr) (ScrnInfoPtr pScrn, int depth);

extern _X_EXPORT Bool
 XAAInitDualFramebufferOverlay(ScreenPtr pScreen, DepthChangeFuncPtr callback);

#endif                          /* _XAA_H */
