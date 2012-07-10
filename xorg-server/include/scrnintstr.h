/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifndef SCREENINTSTRUCT_H
#define SCREENINTSTRUCT_H

#include "screenint.h"
#include "regionstr.h"
#include "colormap.h"
#include "cursor.h"
#include "validate.h"
#include <X11/Xproto.h>
#include "dix.h"
#include "privates.h"

typedef struct _PixmapFormat {
    unsigned char depth;
    unsigned char bitsPerPixel;
    unsigned char scanlinePad;
} PixmapFormatRec;

typedef struct _Visual {
    VisualID vid;
    short class;
    short bitsPerRGBValue;
    short ColormapEntries;
    short nplanes;              /* = log2 (ColormapEntries). This does not
                                 * imply that the screen has this many planes.
                                 * it may have more or fewer */
    unsigned long redMask, greenMask, blueMask;
    int offsetRed, offsetGreen, offsetBlue;
} VisualRec;

typedef struct _Depth {
    unsigned char depth;
    short numVids;
    VisualID *vids;             /* block of visual ids for this depth */
} DepthRec;

typedef struct _ScreenSaverStuff {
    WindowPtr pWindow;
    XID wid;
    char blanked;
    Bool (*ExternalScreenSaver) (ScreenPtr /*pScreen */ ,
                                 int /*xstate */ ,
                                 Bool /*force */ );
} ScreenSaverStuffRec;

/*
 *  There is a typedef for each screen function pointer so that code that
 *  needs to declare a screen function pointer (e.g. in a screen private
 *  or as a local variable) can easily do so and retain full type checking.
 */

typedef Bool (*CloseScreenProcPtr) (ScreenPtr /*pScreen */ );

typedef void (*QueryBestSizeProcPtr) (int /*class */ ,
                                      unsigned short * /*pwidth */ ,
                                      unsigned short * /*pheight */ ,
                                      ScreenPtr /*pScreen */ );

typedef Bool (*SaveScreenProcPtr) (ScreenPtr /*pScreen */ ,
                                   int /*on */ );

typedef void (*GetImageProcPtr) (DrawablePtr /*pDrawable */ ,
                                 int /*sx */ ,
                                 int /*sy */ ,
                                 int /*w */ ,
                                 int /*h */ ,
                                 unsigned int /*format */ ,
                                 unsigned long /*planeMask */ ,
                                 char * /*pdstLine */ );

typedef void (*GetSpansProcPtr) (DrawablePtr /*pDrawable */ ,
                                 int /*wMax */ ,
                                 DDXPointPtr /*ppt */ ,
                                 int * /*pwidth */ ,
                                 int /*nspans */ ,
                                 char * /*pdstStart */ );

typedef void (*SourceValidateProcPtr) (DrawablePtr /*pDrawable */ ,
                                       int /*x */ ,
                                       int /*y */ ,
                                       int /*width */ ,
                                       int /*height */ ,
                                       unsigned int /*subWindowMode */ );

typedef Bool (*CreateWindowProcPtr) (WindowPtr /*pWindow */ );

typedef Bool (*DestroyWindowProcPtr) (WindowPtr /*pWindow */ );

typedef Bool (*PositionWindowProcPtr) (WindowPtr /*pWindow */ ,
                                       int /*x */ ,
                                       int /*y */ );

typedef Bool (*ChangeWindowAttributesProcPtr) (WindowPtr /*pWindow */ ,
                                               unsigned long /*mask */ );

typedef Bool (*RealizeWindowProcPtr) (WindowPtr /*pWindow */ );

typedef Bool (*UnrealizeWindowProcPtr) (WindowPtr /*pWindow */ );

typedef void (*RestackWindowProcPtr) (WindowPtr /*pWindow */ ,
                                      WindowPtr /*pOldNextSib */ );

typedef int (*ValidateTreeProcPtr) (WindowPtr /*pParent */ ,
                                    WindowPtr /*pChild */ ,
                                    VTKind /*kind */ );

typedef void (*PostValidateTreeProcPtr) (WindowPtr /*pParent */ ,
                                         WindowPtr /*pChild */ ,
                                         VTKind /*kind */ );

typedef void (*WindowExposuresProcPtr) (WindowPtr /*pWindow */ ,
                                        RegionPtr /*prgn */ ,
                                        RegionPtr /*other_exposed */ );

typedef void (*CopyWindowProcPtr) (WindowPtr /*pWindow */ ,
                                   DDXPointRec /*ptOldOrg */ ,
                                   RegionPtr /*prgnSrc */ );

typedef void (*ClearToBackgroundProcPtr) (WindowPtr /*pWindow */ ,
                                          int /*x */ ,
                                          int /*y */ ,
                                          int /*w */ ,
                                          int /*h */ ,
                                          Bool /*generateExposures */ );

typedef void (*ClipNotifyProcPtr) (WindowPtr /*pWindow */ ,
                                   int /*dx */ ,
                                   int /*dy */ );

/* pixmap will exist only for the duration of the current rendering operation */
#define CREATE_PIXMAP_USAGE_SCRATCH                     1
/* pixmap will be the backing pixmap for a redirected window */
#define CREATE_PIXMAP_USAGE_BACKING_PIXMAP              2
/* pixmap will contain a glyph */
#define CREATE_PIXMAP_USAGE_GLYPH_PICTURE               3
/* pixmap will be shared */
#define CREATE_PIXMAP_USAGE_SHARED                      4

typedef PixmapPtr (*CreatePixmapProcPtr) (ScreenPtr /*pScreen */ ,
                                          int /*width */ ,
                                          int /*height */ ,
                                          int /*depth */ ,
                                          unsigned /*usage_hint */ );

typedef Bool (*DestroyPixmapProcPtr) (PixmapPtr /*pPixmap */ );

typedef Bool (*RealizeFontProcPtr) (ScreenPtr /*pScreen */ ,
                                    FontPtr /*pFont */ );

typedef Bool (*UnrealizeFontProcPtr) (ScreenPtr /*pScreen */ ,
                                      FontPtr /*pFont */ );

typedef void (*ConstrainCursorProcPtr) (DeviceIntPtr /*pDev */ ,
                                        ScreenPtr /*pScreen */ ,
                                        BoxPtr /*pBox */ );

typedef void (*CursorLimitsProcPtr) (DeviceIntPtr /* pDev */ ,
                                     ScreenPtr /*pScreen */ ,
                                     CursorPtr /*pCursor */ ,
                                     BoxPtr /*pHotBox */ ,
                                     BoxPtr /*pTopLeftBox */ );

typedef Bool (*DisplayCursorProcPtr) (DeviceIntPtr /* pDev */ ,
                                      ScreenPtr /*pScreen */ ,
                                      CursorPtr /*pCursor */ );

typedef Bool (*RealizeCursorProcPtr) (DeviceIntPtr /* pDev */ ,
                                      ScreenPtr /*pScreen */ ,
                                      CursorPtr /*pCursor */ );

typedef Bool (*UnrealizeCursorProcPtr) (DeviceIntPtr /* pDev */ ,
                                        ScreenPtr /*pScreen */ ,
                                        CursorPtr /*pCursor */ );

typedef void (*RecolorCursorProcPtr) (DeviceIntPtr /* pDev */ ,
                                      ScreenPtr /*pScreen */ ,
                                      CursorPtr /*pCursor */ ,
                                      Bool /*displayed */ );

typedef Bool (*SetCursorPositionProcPtr) (DeviceIntPtr /* pDev */ ,
                                          ScreenPtr /*pScreen */ ,
                                          int /*x */ ,
                                          int /*y */ ,
                                          Bool /*generateEvent */ );

typedef Bool (*CreateGCProcPtr) (GCPtr /*pGC */ );

typedef Bool (*CreateColormapProcPtr) (ColormapPtr /*pColormap */ );

typedef void (*DestroyColormapProcPtr) (ColormapPtr /*pColormap */ );

typedef void (*InstallColormapProcPtr) (ColormapPtr /*pColormap */ );

typedef void (*UninstallColormapProcPtr) (ColormapPtr /*pColormap */ );

typedef int (*ListInstalledColormapsProcPtr) (ScreenPtr /*pScreen */ ,
                                              XID * /*pmaps */ );

typedef void (*StoreColorsProcPtr) (ColormapPtr /*pColormap */ ,
                                    int /*ndef */ ,
                                    xColorItem * /*pdef */ );

typedef void (*ResolveColorProcPtr) (unsigned short * /*pred */ ,
                                     unsigned short * /*pgreen */ ,
                                     unsigned short * /*pblue */ ,
                                     VisualPtr /*pVisual */ );

typedef RegionPtr (*BitmapToRegionProcPtr) (PixmapPtr /*pPix */ );

typedef void (*SendGraphicsExposeProcPtr) (ClientPtr /*client */ ,
                                           RegionPtr /*pRgn */ ,
                                           XID /*drawable */ ,
                                           int /*major */ ,
                                           int /*minor */ );

typedef void (*ScreenBlockHandlerProcPtr) (ScreenPtr /*pScreen*/ ,
                                           pointer /*pTimeout */ ,
                                           pointer /*pReadmask */ );

typedef void (*ScreenWakeupHandlerProcPtr) (ScreenPtr /*pScreen*/ ,
                                            unsigned long /*result */ ,
                                            pointer /*pReadMask */ );

typedef Bool (*CreateScreenResourcesProcPtr) (ScreenPtr /*pScreen */ );

typedef Bool (*ModifyPixmapHeaderProcPtr) (PixmapPtr /*pPixmap */ ,
                                           int /*width */ ,
                                           int /*height */ ,
                                           int /*depth */ ,
                                           int /*bitsPerPixel */ ,
                                           int /*devKind */ ,
                                           pointer /*pPixData */ );

typedef PixmapPtr (*GetWindowPixmapProcPtr) (WindowPtr /*pWin */ );

typedef void (*SetWindowPixmapProcPtr) (WindowPtr /*pWin */ ,
                                        PixmapPtr /*pPix */ );

typedef PixmapPtr (*GetScreenPixmapProcPtr) (ScreenPtr /*pScreen */ );

typedef void (*SetScreenPixmapProcPtr) (PixmapPtr /*pPix */ );

typedef void (*MarkWindowProcPtr) (WindowPtr /*pWin */ );

typedef Bool (*MarkOverlappedWindowsProcPtr) (WindowPtr /*parent */ ,
                                              WindowPtr /*firstChild */ ,
                                              WindowPtr * /*pLayerWin */ );

typedef int (*ConfigNotifyProcPtr) (WindowPtr /*pWin */ ,
                                    int /*x */ ,
                                    int /*y */ ,
                                    int /*w */ ,
                                    int /*h */ ,
                                    int /*bw */ ,
                                    WindowPtr /*pSib */ );

typedef void (*MoveWindowProcPtr) (WindowPtr /*pWin */ ,
                                   int /*x */ ,
                                   int /*y */ ,
                                   WindowPtr /*pSib */ ,
                                   VTKind /*kind */ );

typedef void (*ResizeWindowProcPtr) (WindowPtr /*pWin */ ,
                                     int /*x */ ,
                                     int /*y */ ,
                                     unsigned int /*w */ ,
                                     unsigned int /*h */ ,
                                     WindowPtr  /*pSib */
    );

typedef WindowPtr (*GetLayerWindowProcPtr) (WindowPtr   /*pWin */
    );

typedef void (*HandleExposuresProcPtr) (WindowPtr /*pWin */ );

typedef void (*ReparentWindowProcPtr) (WindowPtr /*pWin */ ,
                                       WindowPtr /*pPriorParent */ );

typedef void (*SetShapeProcPtr) (WindowPtr /*pWin */ ,
                                 int /* kind */ );

typedef void (*ChangeBorderWidthProcPtr) (WindowPtr /*pWin */ ,
                                          unsigned int /*width */ );

typedef void (*MarkUnrealizedWindowProcPtr) (WindowPtr /*pChild */ ,
                                             WindowPtr /*pWin */ ,
                                             Bool /*fromConfigure */ );

typedef Bool (*DeviceCursorInitializeProcPtr) (DeviceIntPtr /* pDev */ ,
                                               ScreenPtr /* pScreen */ );

typedef void (*DeviceCursorCleanupProcPtr) (DeviceIntPtr /* pDev */ ,
                                            ScreenPtr /* pScreen */ );

typedef void (*ConstrainCursorHarderProcPtr) (DeviceIntPtr, ScreenPtr, int,
                                              int *, int *);


typedef Bool (*SharePixmapBackingProcPtr)(PixmapPtr, ScreenPtr, void **);

typedef Bool (*SetSharedPixmapBackingProcPtr)(PixmapPtr, void *);

typedef Bool (*StartPixmapTrackingProcPtr)(PixmapPtr, PixmapPtr,
                                           int x, int y);

typedef Bool (*StopPixmapTrackingProcPtr)(PixmapPtr, PixmapPtr);

typedef Bool (*ReplaceScanoutPixmapProcPtr)(DrawablePtr, PixmapPtr, Bool);

typedef struct _Screen {
    int myNum;                  /* index of this instance in Screens[] */
    ATOM id;
    short x, y, width, height;
    short mmWidth, mmHeight;
    short numDepths;
    unsigned char rootDepth;
    DepthPtr allowedDepths;
    unsigned long rootVisual;
    unsigned long defColormap;
    short minInstalledCmaps, maxInstalledCmaps;
    char backingStoreSupport, saveUnderSupport;
    unsigned long whitePixel, blackPixel;
    GCPtr GCperDepth[MAXFORMATS + 1];
    /* next field is a stipple to use as default in
       a GC.  we don't build default tiles of all depths
       because they are likely to be of a color
       different from the default fg pixel, so
       we don't win anything by building
       a standard one.
     */
    PixmapPtr PixmapPerDepth[1];
    pointer devPrivate;
    short numVisuals;
    VisualPtr visuals;
    WindowPtr root;
    ScreenSaverStuffRec screensaver;

    DevPrivateSetRec    screenSpecificPrivates[PRIVATE_LAST];

    /* Random screen procedures */

    CloseScreenProcPtr CloseScreen;
    QueryBestSizeProcPtr QueryBestSize;
    SaveScreenProcPtr SaveScreen;
    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;
    SourceValidateProcPtr SourceValidate;

    /* Window Procedures */

    CreateWindowProcPtr CreateWindow;
    DestroyWindowProcPtr DestroyWindow;
    PositionWindowProcPtr PositionWindow;
    ChangeWindowAttributesProcPtr ChangeWindowAttributes;
    RealizeWindowProcPtr RealizeWindow;
    UnrealizeWindowProcPtr UnrealizeWindow;
    ValidateTreeProcPtr ValidateTree;
    PostValidateTreeProcPtr PostValidateTree;
    WindowExposuresProcPtr WindowExposures;
    CopyWindowProcPtr CopyWindow;
    ClearToBackgroundProcPtr ClearToBackground;
    ClipNotifyProcPtr ClipNotify;
    RestackWindowProcPtr RestackWindow;

    /* Pixmap procedures */

    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;

    /* Font procedures */

    RealizeFontProcPtr RealizeFont;
    UnrealizeFontProcPtr UnrealizeFont;

    /* Cursor Procedures */

    ConstrainCursorProcPtr ConstrainCursor;
    ConstrainCursorHarderProcPtr ConstrainCursorHarder;
    CursorLimitsProcPtr CursorLimits;
    DisplayCursorProcPtr DisplayCursor;
    RealizeCursorProcPtr RealizeCursor;
    UnrealizeCursorProcPtr UnrealizeCursor;
    RecolorCursorProcPtr RecolorCursor;
    SetCursorPositionProcPtr SetCursorPosition;

    /* GC procedures */

    CreateGCProcPtr CreateGC;

    /* Colormap procedures */

    CreateColormapProcPtr CreateColormap;
    DestroyColormapProcPtr DestroyColormap;
    InstallColormapProcPtr InstallColormap;
    UninstallColormapProcPtr UninstallColormap;
    ListInstalledColormapsProcPtr ListInstalledColormaps;
    StoreColorsProcPtr StoreColors;
    ResolveColorProcPtr ResolveColor;

    /* Region procedures */

    BitmapToRegionProcPtr BitmapToRegion;
    SendGraphicsExposeProcPtr SendGraphicsExpose;

    /* os layer procedures */

    ScreenBlockHandlerProcPtr BlockHandler;
    ScreenWakeupHandlerProcPtr WakeupHandler;

    /* anybody can get a piece of this array */
    PrivateRec *devPrivates;

    CreateScreenResourcesProcPtr CreateScreenResources;
    ModifyPixmapHeaderProcPtr ModifyPixmapHeader;

    GetWindowPixmapProcPtr GetWindowPixmap;
    SetWindowPixmapProcPtr SetWindowPixmap;
    GetScreenPixmapProcPtr GetScreenPixmap;
    SetScreenPixmapProcPtr SetScreenPixmap;

    PixmapPtr pScratchPixmap;   /* scratch pixmap "pool" */

    unsigned int totalPixmapSize;

    MarkWindowProcPtr MarkWindow;
    MarkOverlappedWindowsProcPtr MarkOverlappedWindows;
    ConfigNotifyProcPtr ConfigNotify;
    MoveWindowProcPtr MoveWindow;
    ResizeWindowProcPtr ResizeWindow;
    GetLayerWindowProcPtr GetLayerWindow;
    HandleExposuresProcPtr HandleExposures;
    ReparentWindowProcPtr ReparentWindow;

    SetShapeProcPtr SetShape;

    ChangeBorderWidthProcPtr ChangeBorderWidth;
    MarkUnrealizedWindowProcPtr MarkUnrealizedWindow;

    /* Device cursor procedures */
    DeviceCursorInitializeProcPtr DeviceCursorInitialize;
    DeviceCursorCleanupProcPtr DeviceCursorCleanup;

    /* set it in driver side if X server can copy the framebuffer content.
     * Meant to be used together with '-background none' option, avoiding
     * malicious users to steal framebuffer's content if that would be the
     * default */
    Bool canDoBGNoneRoot;

    Bool isGPU;

    struct xorg_list unattached_list;
    struct xorg_list unattached_head;

    ScreenPtr current_master;

    struct xorg_list output_slave_list;
    struct xorg_list output_head;

    SharePixmapBackingProcPtr SharePixmapBacking;
    SetSharedPixmapBackingProcPtr SetSharedPixmapBacking;

    StartPixmapTrackingProcPtr StartPixmapTracking;
    StopPixmapTrackingProcPtr StopPixmapTracking;

    struct xorg_list pixmap_dirty_list;
    struct xorg_list offload_slave_list;
    struct xorg_list offload_head;

    ReplaceScanoutPixmapProcPtr ReplaceScanoutPixmap;
} ScreenRec;

static inline RegionPtr
BitmapToRegion(ScreenPtr _pScreen, PixmapPtr pPix)
{
    return (*(_pScreen)->BitmapToRegion) (pPix);        /* no mi version?! */
}

typedef struct _ScreenInfo {
    int imageByteOrder;
    int bitmapScanlineUnit;
    int bitmapScanlinePad;
    int bitmapBitOrder;
    int numPixmapFormats;
     PixmapFormatRec formats[MAXFORMATS];
    int numScreens;
    ScreenPtr screens[MAXSCREENS];
    int numGPUScreens;
    ScreenPtr gpuscreens[MAXGPUSCREENS];
    int x;                      /* origin */
    int y;                      /* origin */
    int width;                  /* total width of all screens together */
    int height;                 /* total height of all screens together */
} ScreenInfo;

extern _X_EXPORT ScreenInfo screenInfo;

extern _X_EXPORT void InitOutput(ScreenInfo * /*pScreenInfo */ ,
                                 int /*argc */ ,
                                 char ** /*argv */ );

#endif                          /* SCREENINTSTRUCT_H */
