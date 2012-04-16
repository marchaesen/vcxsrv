/*
 * Copyright 2001-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *   David H. Dawes <dawes@xfree86.org>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Main header file included by all other DMX-related files.
 */

/** \mainpage
 * - <a href="http://dmx.sourceforge.net">DMX Home Page</a>
 * - <a href="http://sourceforge.net/projects/dmx">DMX Project Page (on
 * Source Forge)</a>
 * - <a href="http://dmx.sourceforge.net/dmx.html">Distributed Multihead
 * X design</a>, the design document for DMX
 * - <a href="http://dmx.sourceforge.net/DMXSpec.txt">Client-to-Server
 * DMX Extension to the X Protocol</a>
 */

#ifndef DMX_H
#define DMX_H

#if HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "gcstruct.h"

/* Handle client-side include files in one place. */
#include "dmxclient.h"

#include "globals.h"
#include "scrnintstr.h"

#include "picturestr.h"

#ifdef GLXEXT
#include <GL/glx.h>
#include <GL/glxint.h>
#endif

typedef enum {
    PosNone = -1,
    PosAbsolute = 0,
    PosRightOf,
    PosLeftOf,
    PosAbove,
    PosBelow,
    PosRelative
} PositionType;

/** Provide the typedef globally, but keep the contents opaque outside
 * of the input routines.  \see dmxinput.h */
typedef struct _DMXInputInfo DMXInputInfo;

/** Provide the typedef globally, but keep the contents opaque outside
 * of the XSync statistic routines.  \see dmxstat.c */
typedef struct _DMXStatInfo DMXStatInfo;

/** Global structure containing information about each backend screen. */
typedef struct _DMXScreenInfo {
    const char *name;             /**< Name from command line or config file */
    int index;                    /**< Index into dmxScreens global          */

    /*---------- Back-end X server information ----------*/

    Display *beDisplay;           /**< Back-end X server's display */
    int beWidth;                  /**< Width of BE display */
    int beHeight;                 /**< Height of BE display */
    int beDepth;                  /**< Depth of BE display */
    int beBPP;                    /**< Bits per pixel of BE display */
    int beXDPI;                   /**< Horizontal dots per inch of BE */
    int beYDPI;                   /**< Vertical dots per inch of BE */

    int beNumDepths;              /**< Number of depths on BE server */
    int *beDepths;                /**< Depths from BE server */

    int beNumPixmapFormats;           /**< Number of pixmap formats on BE */
    XPixmapFormatValues *bePixmapFormats; /**< Pixmap formats on BE */

    int beNumVisuals;             /**< Number of visuals on BE */
    XVisualInfo *beVisuals;       /**< Visuals from BE server */
    int beDefVisualIndex;           /**< Default visual index of BE */

    int beNumDefColormaps;           /**< Number of default colormaps */
    Colormap *beDefColormaps;     /**< Default colormaps for DMX server */

    Pixel beBlackPixel;           /**< Default black pixel for BE */
    Pixel beWhitePixel;           /**< Default white pixel for BE */

    /*---------- Screen window information ----------*/

    Window scrnWin;               /**< "Screen" window on backend display */
    int scrnX;                    /**< X offset of "screen" WRT BE display */
    int scrnY;                    /**< Y offset of "screen" WRT BE display */
    int scrnWidth;                /**< Width of "screen" */
    int scrnHeight;               /**< Height of "screen" */
    int scrnXSign;                /**< X offset sign of "screen" */
    int scrnYSign;                /**< Y offset sign of "screen" */

                                  /** Default drawables for "screen" */
    Drawable scrnDefDrawables[MAXFORMATS];

    struct _DMXScreenInfo *next;  /**< List of "screens" on same display */
    struct _DMXScreenInfo *over;  /**< List of "screens" that overlap */

    /*---------- Root window information ----------*/

    Window rootWin;               /**< "Root" window on backend display */
    int rootX;                    /**< X offset of "root" window WRT "screen"*/
    int rootY;                    /**< Y offset of "root" window WRT "screen"*/
    int rootWidth;                /**< Width of "root" window */
    int rootHeight;               /**< Height of "root" window */

    int rootXOrigin;              /**< Global X origin of "root" window */
    int rootYOrigin;              /**< Global Y origin of "root" window */

    /*---------- Shadow framebuffer information ----------*/

    void *shadow;                 /**< Shadow framebuffer data (if enabled) */
    XlibGC shadowGC;              /**< Default GC used by shadow FB code */
    XImage *shadowFBImage;        /**< Screen image used by shadow FB code */

    /*---------- Other related information ----------*/

    int shared;                   /**< Non-zero if another Xdmx is running */

    Bool WMRunningOnBE;

    Cursor noCursor;
    Cursor curCursor;
    /* Support for cursors on overlapped
     * backend displays. */
    CursorPtr cursor;
    int cursorVisible;
    int cursorNotShared;        /* for overlapping screens on a backend */

    PositionType where;             /**< Relative layout information */
    int whereX;                     /**< Relative layout information */
    int whereY;                     /**< Relative layout information */
    int whereRefScreen;             /**< Relative layout information */

    int savedTimeout;               /**< Original screen saver timeout */
    int dpmsCapable;                /**< Non-zero if backend is DPMS capable */
    int dpmsEnabled;                /**< Non-zero if DPMS enabled */
    int dpmsStandby;                /**< Original DPMS standby value  */
    int dpmsSuspend;                /**< Original DPMS suspend value  */
    int dpmsOff;                    /**< Original DPMS off value  */

    DMXStatInfo *stat;              /**< Statistics about XSync  */
    Bool needsSync;                 /**< True if an XSync is pending  */

#ifdef GLXEXT
                                  /** Visual information for glxProxy */
    int numGlxVisuals;
    __GLXvisualConfig *glxVisuals;
    int glxMajorOpcode;
    int glxErrorBase;

                                  /** FB config information for glxProxy */
    __GLXFBConfig *fbconfigs;
    int numFBConfigs;
#endif

                                    /** Function pointers to wrapped screen
				     *  functions */
    CloseScreenProcPtr CloseScreen;
    SaveScreenProcPtr SaveScreen;

    CreateGCProcPtr CreateGC;

    CreateWindowProcPtr CreateWindow;
    DestroyWindowProcPtr DestroyWindow;
    PositionWindowProcPtr PositionWindow;
    ChangeWindowAttributesProcPtr ChangeWindowAttributes;
    RealizeWindowProcPtr RealizeWindow;
    UnrealizeWindowProcPtr UnrealizeWindow;
    RestackWindowProcPtr RestackWindow;
    WindowExposuresProcPtr WindowExposures;
    CopyWindowProcPtr CopyWindow;

    ResizeWindowProcPtr ResizeWindow;
    ReparentWindowProcPtr ReparentWindow;

    ChangeBorderWidthProcPtr ChangeBorderWidth;

    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;

    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;
    BitmapToRegionProcPtr BitmapToRegion;

    RealizeFontProcPtr RealizeFont;
    UnrealizeFontProcPtr UnrealizeFont;

    CreateColormapProcPtr CreateColormap;
    DestroyColormapProcPtr DestroyColormap;
    InstallColormapProcPtr InstallColormap;
    StoreColorsProcPtr StoreColors;

    SetShapeProcPtr SetShape;

    CreatePictureProcPtr CreatePicture;
    DestroyPictureProcPtr DestroyPicture;
    ChangePictureClipProcPtr ChangePictureClip;
    DestroyPictureClipProcPtr DestroyPictureClip;

    ChangePictureProcPtr ChangePicture;
    ValidatePictureProcPtr ValidatePicture;

    CompositeProcPtr Composite;
    GlyphsProcPtr Glyphs;
    CompositeRectsProcPtr CompositeRects;

    InitIndexedProcPtr InitIndexed;
    CloseIndexedProcPtr CloseIndexed;
    UpdateIndexedProcPtr UpdateIndexed;

    TrapezoidsProcPtr Trapezoids;
    TrianglesProcPtr Triangles;
} DMXScreenInfo;

/* Global variables available to all Xserver/hw/dmx routines. */
extern int dmxNumScreens;                       /**< Number of dmxScreens */
extern DMXScreenInfo *dmxScreens;               /**< List of outputs */
extern int dmxShadowFB;                         /**< Non-zero if using

                                                 * shadow frame-buffer
                                                 * (deprecated) */
extern XErrorEvent dmxLastErrorEvent;           /**< Last error that

                                                 * occurred */
extern Bool dmxErrorOccurred;                   /**< True if an error

                                                 * occurred */
extern Bool dmxOffScreenOpt;                    /**< True if using off

                                                 * screen
                                                 * optimizations */
extern Bool dmxSubdividePrimitives;             /**< True if using the

                                                 * primitive subdivision
                                                 * optimization */
extern Bool dmxLazyWindowCreation;              /**< True if using the

                                                 * lazy window creation
                                                 * optimization */
extern Bool dmxUseXKB;                          /**< True if the XKB

                                                 * extension should be
                                                 * used with the backend
                                                 * servers */
extern int dmxDepth;                            /**< Requested depth if

                                                 * non-zero */
#ifdef GLXEXT
extern Bool dmxGLXProxy;                        /**< True if glxProxy

						 * support is enabled */
extern Bool dmxGLXSwapGroupSupport;             /**< True if glxProxy

						 * support for swap
						 * groups and barriers
						 * is enabled */
extern Bool dmxGLXSyncSwap;                     /**< True if glxProxy

						 * should force an XSync
						 * request after each
						 * swap buffers call */
extern Bool dmxGLXFinishSwap;                   /**< True if glxProxy

						 * should force a
						 * glFinish request
						 * after each swap
						 * buffers call */
#endif
extern char *dmxFontPath;                       /**< NULL if no font

						 * path is set on the
						 * command line;
						 * otherwise, a string
						 * of comma separated
						 * paths built from the
						 * command line
						 * specified font
						 * paths */
extern Bool dmxIgnoreBadFontPaths;              /**< True if bad font

						 * paths should be
						 * ignored during server
						 * init */
extern Bool dmxAddRemoveScreens;                /**< True if add and

						 * remove screens support
						 * is enabled */

/** Wrap screen or GC function pointer */
#define DMX_WRAP(_entry, _newfunc, _saved, _actual)			\
do {									\
    (_saved)->_entry  = (_actual)->_entry;				\
    (_actual)->_entry = (_newfunc);					\
} while (0)

/** Unwrap screen or GC function pointer */
#define DMX_UNWRAP(_entry, _saved, _actual)				\
do {									\
    (_actual)->_entry = (_saved)->_entry;				\
} while (0)

/* Define the MAXSCREENSALLOC/FREE macros, when MAXSCREENS patch has not
 * been applied to sources. */
#ifdef MAXSCREENS
#define MAXSCREEN_MAKECONSTSTR1(x) #x
#define MAXSCREEN_MAKECONSTSTR2(x) MAXSCREEN_MAKECONSTSTR1(x)

#define MAXSCREEN_FAILED_TXT "Failed at ["                              \
   MAXSCREEN_MAKECONSTSTR2(__LINE__) ":" __FILE__ "] to allocate object: "

#define _MAXSCREENSALLOCF(o,size,fatal)                                 \
    do {                                                                \
        if (!o) {                                                       \
            o = calloc((size), sizeof(*(o)));                          \
            if (!o && fatal) FatalError(MAXSCREEN_FAILED_TXT #o);       \
        }                                                               \
    } while (0)
#define _MAXSCREENSALLOCR(o,size,retval)                                \
    do {                                                                \
        if (!o) {                                                       \
            o = calloc((size), sizeof(*(o)));                          \
            if (!o) return retval;                                      \
        }                                                               \
    } while (0)

#define MAXSCREENSFREE(o)                                               \
    do {                                                                \
        free(o);                                                \
        o = NULL;                                                       \
    } while (0)

#define MAXSCREENSALLOC(o)              _MAXSCREENSALLOCF(o,MAXSCREENS,  0)
#define MAXSCREENSALLOC_FATAL(o)        _MAXSCREENSALLOCF(o,MAXSCREENS,  1)
#define MAXSCREENSALLOC_RETURN(o,r)     _MAXSCREENSALLOCR(o,MAXSCREENS, (r))
#define MAXSCREENSALLOCPLUSONE(o)       _MAXSCREENSALLOCF(o,MAXSCREENS+1,0)
#define MAXSCREENSALLOCPLUSONE_FATAL(o) _MAXSCREENSALLOCF(o,MAXSCREENS+1,1)
#define MAXSCREENSCALLOC(o,m)           _MAXSCREENSALLOCF(o,MAXSCREENS*(m),0)
#define MAXSCREENSCALLOC_FATAL(o,m)     _MAXSCREENSALLOCF(o,MAXSCREENS*(m),1)
#endif

extern DevPrivateKeyRec dmxGCPrivateKeyRec;

#define dmxGCPrivateKey (&dmxGCPrivateKeyRec) /**< Private index for GCs       */

extern DevPrivateKeyRec dmxWinPrivateKeyRec;

#define dmxWinPrivateKey (&dmxWinPrivateKeyRec) /**< Private index for Windows   */

extern DevPrivateKeyRec dmxPixPrivateKeyRec;

#define dmxPixPrivateKey (&dmxPixPrivateKeyRec) /**< Private index for Pixmaps   */

extern int dmxFontPrivateIndex;        /**< Private index for Fonts     */

extern DevPrivateKeyRec dmxScreenPrivateKeyRec;

#define dmxScreenPrivateKey (&dmxScreenPrivateKeyRec) /**< Private index for Screens   */

extern DevPrivateKeyRec dmxColormapPrivateKeyRec;

#define dmxColormapPrivateKey (&dmxColormapPrivateKeyRec) /**< Private index for Colormaps */

extern DevPrivateKeyRec dmxPictPrivateKeyRec;

#define dmxPictPrivateKey (&dmxPictPrivateKeyRec) /**< Private index for Picts     */

extern DevPrivateKeyRec dmxGlyphSetPrivateKeyRec;

#define dmxGlyphSetPrivateKey (&dmxGlyphSetPrivateKeyRec) /**< Private index for GlyphSets */

#endif                          /* DMX_H */
